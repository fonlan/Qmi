#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "qmi_settings.h"
#include "qmi_app.h"
#include "qmi_utils.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif
#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#endif
#ifndef TBS_TRANSPARENTBKGND
#define TBS_TRANSPARENTBKGND 0x1000
#endif

namespace {
constexpr wchar_t kMainClassName[] = L"QmiMainWindowClass";

constexpr UINT kMenuOpenFile = 1001;
constexpr UINT kMenuCopyImage = 1002;
constexpr UINT kMenuCopyFile = 1003;
constexpr UINT kMenuDeleteFile = 1004;
constexpr UINT kMenuExit = 1006;
constexpr UINT kMenuOpenContainingFolder = 1007;
constexpr UINT kMenuCopyImagePath = 1008;
constexpr UINT kMenuRotateClockwise = 1009;
constexpr UINT kMenuRotateCounterclockwise = 1010;
constexpr UINT kMenuFlipHorizontal = 1011;
constexpr UINT kMenuFlipVertical = 1012;
constexpr UINT kMenuPrintImage = 1013;
constexpr UINT kMenuToggleTopMost = 1014;
constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT_PTR kStartupScanTimerId = 2;
constexpr UINT_PTR kAnimationTimerId = 3;

constexpr int kCtrlFitOnSwitch = 2001;
constexpr int kCtrlSmoothSampling = 2002;
constexpr int kCtrlWindowOpacitySlider = 2003;
constexpr int kCtrlWindowOpacityLabel = 2004;
constexpr int kCtrlWindowOpacityValue = 2005;
constexpr int kCtrlFilmStripSortFieldLabel = 2006;
constexpr int kCtrlFilmStripSortFieldCombo = 2007;
constexpr int kCtrlFilmStripSortDirectionLabel = 2008;
constexpr int kCtrlFilmStripSortDirectionCombo = 2009;

constexpr int kMinWindowWidth = 640;
constexpr int kMinWindowHeight = 420;
constexpr int kResizeBorder = 8;
constexpr int kTitleButtonWidth = 46;
constexpr int kTitleButtonHeight = 34;
constexpr float kViewportMargin = 0.0f;
constexpr float kViewportBottomGap = 0.0f;
constexpr ULONGLONG kInteractiveFrameIntervalMs = 16;
constexpr int kThumbnailDecodeBudgetPerFrame = 1;
constexpr float kHoverOverlayOpacity = 0.14f;
constexpr float kCloseHoverOverlayOpacity = 0.82f;
constexpr float kOpenButtonFillOpacity = 200.0f / 255.0f;
constexpr float kOpenButtonHoverOpacity = 0.14f;
constexpr float kOpenButtonStrokeOpacity = 200.0f / 255.0f;
constexpr float kFilmStripLargeScale = 1.08f;
constexpr float kFilmStripMediumScale = 1.00f;
constexpr float kFilmStripSmallScale = 0.74f;
constexpr float kFilmStripHoverScaleBoost = 0.13f;
constexpr float kFilmStripScaleLerp = 0.28f;

template <typename T>
T Clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

int ClampWindowOpacityPercent(int percent) {
    return Clamp(percent, kMinWindowOpacityPercent, kMaxWindowOpacityPercent);
}

float GetBackgroundOpacityScale(int percent) {
    return static_cast<float>(ClampWindowOpacityPercent(percent)) / 100.0f;
}

std::wstring FormatWindowOpacityPercentText(int percent) {
    return std::to_wstring(ClampWindowOpacityPercent(percent)) + L"%";
}

void UpdateWindowOpacityLabel(SettingsWindowState* state, int percent) {
    if (!state || !state->opacity_value_label) {
        return;
    }

    HWND label_hwnd = state->opacity_value_label;
    const std::wstring text = FormatWindowOpacityPercentText(percent);
    SetWindowTextW(label_hwnd, text.c_str());

    HWND parent_hwnd = GetParent(label_hwnd);
    RECT label_rect{};
    if (parent_hwnd && GetWindowRect(label_hwnd, &label_rect)) {
        MapWindowPoints(nullptr, parent_hwnd, reinterpret_cast<POINT*>(&label_rect), 2);
        RedrawWindow(parent_hwnd, &label_rect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
    RedrawWindow(label_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

int ClampFilmStripSortDirectionIndex(int index) {
    return index <= 0 ? 0 : 1;
}

int ToFilmStripSortDirectionIndex(bool descending) {
    return descending ? 1 : 0;
}

bool IsFilmStripSortControlId(int control_id) {
    return control_id == kCtrlFilmStripSortFieldCombo || control_id == kCtrlFilmStripSortDirectionCombo;
}

void SyncFilmStripSortControls(SettingsWindowState* state, int sort_field, bool sort_descending) {
    if (!state) {
        return;
    }
    if (state->sort_field_combo) {
        SendMessageW(state->sort_field_combo, CB_SETCURSEL, ClampFilmStripSortField(sort_field), 0);
    }
    if (state->sort_direction_combo) {
        SendMessageW(state->sort_direction_combo, CB_SETCURSEL, ToFilmStripSortDirectionIndex(sort_descending), 0);
    }
}

bool IsRenderableImageType(ImageType type) {
    return type == ImageType::Raster || type == ImageType::Svg;
}
}  // namespace

bool QmiApp::Initialize(HINSTANCE hinstance, int show_cmd, const std::optional<std::wstring>& startup_path) {
    hinstance_ = hinstance;

    const HRESULT coinit_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coinit_hr) && coinit_hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&common_controls)) {
        return false;
    }

    LoadUserConfig(&fit_on_switch_,
                   &smooth_sampling_,
                   &window_opacity_percent_,
                   &film_strip_sort_field_,
                   &film_strip_sort_descending_);
    film_strip_sort_field_ = ClampFilmStripSortField(film_strip_sort_field_);

    if (!InitDeviceIndependentResources()) {
        return false;
    }

    if (!RegisterWindowClasses()) {
        return false;
    }

    if (!CreateMainWindow()) {
        return false;
    }

    ApplyWindowBackdrop();

    if (!InitDeviceResources()) {
        return false;
    }

    TryOpenInitialImage(startup_path);

    const bool use_maximized_startup = show_cmd == SW_HIDE || show_cmd == SW_SHOWNORMAL || show_cmd == SW_NORMAL ||
                                       show_cmd == SW_SHOW || show_cmd == SW_SHOWDEFAULT || show_cmd == SW_RESTORE;
    const int initial_show_cmd = use_maximized_startup ? SW_SHOWMAXIMIZED : show_cmd;
    ShowWindow(hwnd_, initial_show_cmd);
    RequestRender();
    UpdateWindow(hwnd_);
    return true;
}

int QmiApp::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

bool QmiApp::InitDeviceIndependentResources() {
    D2D1_FACTORY_OPTIONS factory_options{};
#if defined(_DEBUG)
    factory_options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory1),
                                 &factory_options,
                                 reinterpret_cast<void**>(d2d_factory_.ReleaseAndGetAddressOf())))) {
        return false;
    }

    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_factory_)))) {
        return false;
    }

    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_factory_.GetAddressOf())))) {
        return false;
    }

    if (FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f,
            L"zh-CN", &text_format_))) {
        return false;
    }
    text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f,
            L"zh-CN", &small_text_format_))) {
        return false;
    }
    small_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    small_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    small_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f,
            L"zh-CN", &info_text_format_))) {
        return false;
    }
    info_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    info_text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    info_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return true;
}

bool QmiApp::RegisterWindowClasses() {
    HICON app_icon = LoadAppIcon(hinstance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    HICON app_icon_small = LoadAppIcon(hinstance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    if (!app_icon) {
        app_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (!app_icon_small) {
        app_icon_small = LoadIconW(nullptr, IDI_APPLICATION);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hinstance_;
    wc.lpfnWndProc = &QmiApp::MainWndProc;
    wc.lpszClassName = kMainClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = app_icon;
    wc.hIconSm = app_icon_small;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    WNDCLASSEXW settings_wc{};
    settings_wc.cbSize = sizeof(settings_wc);
    settings_wc.hInstance = hinstance_;
    settings_wc.lpfnWndProc = &QmiApp::SettingsWndProc;
    settings_wc.lpszClassName = kSettingsClassName;
    settings_wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settings_wc.hIcon = app_icon;
    settings_wc.hIconSm = app_icon_small;
    settings_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&settings_wc)) {
        return false;
    }

    return true;
}

bool QmiApp::CreateMainWindow() {
    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const DWORD ex_style = WS_EX_APPWINDOW | WS_EX_LAYERED;
    hwnd_ = CreateWindowExW(ex_style,
                            kMainClassName,
                            L"Qmi",
                            style,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            1280,
                            840,
                            nullptr,
                            nullptr,
                            hinstance_,
                            this);
    if (!hwnd_) {
        return false;
    }

    if (HICON app_icon = LoadAppIcon(hinstance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON))) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
    }
    if (HICON app_icon_small = LoadAppIcon(hinstance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON))) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));
    }

    DragAcceptFiles(hwnd_, TRUE);
    return true;
}

void QmiApp::ApplyWindowBackdrop() {
    if (!hwnd_) {
        return;
    }

    const MARGINS glass = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &glass);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    int backdrop_type = DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type, sizeof(backdrop_type));

    DWM_BLURBEHIND blur_behind{};
    blur_behind.dwFlags = DWM_BB_ENABLE;
    blur_behind.fEnable = FALSE;
    DwmEnableBlurBehindWindow(hwnd_, &blur_behind);
}

bool QmiApp::InitDeviceResources() {
    if (d2d_context_) {
        return true;
    }

    UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

    if (FAILED(D3D11CreateDevice(nullptr,
                                 D3D_DRIVER_TYPE_HARDWARE,
                                 nullptr,
                                 creation_flags,
                                 feature_levels,
                                 ARRAYSIZE(feature_levels),
                                 D3D11_SDK_VERSION,
                                 &d3d_device_,
                                 &feature_level,
                                 &d3d_context_))) {
        if (FAILED(D3D11CreateDevice(nullptr,
                                     D3D_DRIVER_TYPE_WARP,
                                     nullptr,
                                     creation_flags,
                                     feature_levels,
                                     ARRAYSIZE(feature_levels),
                                     D3D11_SDK_VERSION,
                                     &d3d_device_,
                                     &feature_level,
                                     &d3d_context_))) {
            return false;
        }
    }

    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(d3d_device_.As(&dxgi_device))) {
        return false;
    }

    if (FAILED(d2d_factory_->CreateDevice(dxgi_device.Get(), &d2d_device_))) {
        return false;
    }

    if (FAILED(d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context_))) {
        return false;
    }
    d2d_context_.As(&d2d_context5_);
    bitmaps_need_reload_ = true;

    if (!CreateWindowSizeResources()) {
        return false;
    }

    CreateBrushes();
    return true;
}

bool QmiApp::CreateWindowSizeResources() {
    if (!hwnd_ || !d2d_context_ || !d3d_device_) {
        return false;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    const UINT height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));

    d2d_context_->SetTarget(nullptr);
    target_bitmap_.Reset();
    frame_texture_.Reset();
    readback_texture_.Reset();
    ReleaseLayeredBitmap();

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d3d_device_->CreateTexture2D(&texture_desc, nullptr, &frame_texture_))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC readback_desc = texture_desc;
    readback_desc.Usage = D3D11_USAGE_STAGING;
    readback_desc.BindFlags = 0;
    readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d3d_device_->CreateTexture2D(&readback_desc, nullptr, &readback_texture_))) {
        return false;
    }

    ComPtr<IDXGISurface> target_surface;
    if (FAILED(frame_texture_.As(&target_surface))) {
        return false;
    }

    const D2D1_BITMAP_PROPERTIES1 bitmap_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(d2d_context_->CreateBitmapFromDxgiSurface(target_surface.Get(), &bitmap_props, &target_bitmap_))) {
        return false;
    }

    d2d_context_->SetTarget(target_bitmap_.Get());
    d2d_context_->SetDpi(96.0f, 96.0f);

    if (!layered_dc_) {
        layered_dc_ = CreateCompatibleDC(nullptr);
        if (!layered_dc_) {
            return false;
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = static_cast<LONG>(width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dib_bits = nullptr;
    HBITMAP dib = CreateDIBSection(layered_dc_, &bmi, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
    if (!dib || !dib_bits) {
        if (dib) {
            DeleteObject(dib);
        }
        return false;
    }

    layered_prev_bitmap_ = SelectObject(layered_dc_, dib);
    layered_bitmap_ = dib;
    layered_bits_ = dib_bits;
    layered_width_ = width;
    layered_height_ = height;
    layered_stride_ = width * 4;

    if (bitmaps_need_reload_) {
        thumbnails_.assign(images_.size(), Thumbnail{});
        thumbnail_draw_scales_.assign(images_.size(), 0.0f);
        if (current_index_ >= 0 && current_index_ < static_cast<int>(images_.size())) {
            LoadImageByIndex(current_index_, false);
        }
        bitmaps_need_reload_ = false;
    }
    return true;
}

void QmiApp::ReleaseLayeredBitmap() {
    if (layered_dc_ && layered_bitmap_) {
        SelectObject(layered_dc_, layered_prev_bitmap_);
        DeleteObject(layered_bitmap_);
    }
    layered_bitmap_ = nullptr;
    layered_prev_bitmap_ = nullptr;
    layered_bits_ = nullptr;
    layered_width_ = 0;
    layered_height_ = 0;
    layered_stride_ = 0;
}

void QmiApp::CreateBrushes() {
    if (!d2d_context_) {
        return;
    }

    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xF0F0F0, 0.97f), &brush_text_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x171717, 1.0f), &brush_panel_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x101010, 1.0f), &brush_overlay_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x49A1FF, 1.0f), &brush_accent_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), &brush_hover_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xE81123, 1.0f), &brush_close_hover_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x171717, kOpenButtonFillOpacity), &brush_open_button_fill_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, kOpenButtonHoverOpacity), &brush_open_button_hover_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x101010, kOpenButtonStrokeOpacity), &brush_open_button_stroke_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x111111, 1.0f), &brush_viewport_bg_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x111111, 1.0f), &brush_image_bg_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x6D7685, 1.0f), &brush_thumb_bg_);
    UpdateBackgroundOpacityBrushes();
}

void QmiApp::UpdateBackgroundOpacityBrushes() {
    const float opacity_scale = GetBackgroundOpacityScale(window_opacity_percent_);
    if (brush_panel_) {
        brush_panel_->SetOpacity(opacity_scale);
    }
    if (brush_overlay_) {
        brush_overlay_->SetOpacity(opacity_scale);
    }
    if (brush_hover_) {
        brush_hover_->SetOpacity(kHoverOverlayOpacity * opacity_scale);
    }
    if (brush_close_hover_) {
        brush_close_hover_->SetOpacity(kCloseHoverOverlayOpacity * opacity_scale);
    }
    if (brush_viewport_bg_) {
        brush_viewport_bg_->SetOpacity(opacity_scale);
    }
    if (brush_thumb_bg_) {
        brush_thumb_bg_->SetOpacity(opacity_scale);
    }
}

void QmiApp::DiscardDeviceResources() {
    ClearAnimationState();

    target_bitmap_.Reset();
    frame_texture_.Reset();
    readback_texture_.Reset();
    d2d_context5_.Reset();
    d2d_context_.Reset();
    d2d_device_.Reset();
    d3d_context_.Reset();
    d3d_device_.Reset();
    ReleaseLayeredBitmap();
    if (layered_dc_) {
        DeleteDC(layered_dc_);
        layered_dc_ = nullptr;
    }

    brush_text_.Reset();
    brush_panel_.Reset();
    brush_overlay_.Reset();
    brush_accent_.Reset();
    brush_hover_.Reset();
    brush_close_hover_.Reset();
    brush_open_button_fill_.Reset();
    brush_open_button_hover_.Reset();
    brush_open_button_stroke_.Reset();
    brush_viewport_bg_.Reset();
    brush_image_bg_.Reset();
    brush_thumb_bg_.Reset();
}

void QmiApp::DrawCenteredText(const std::wstring& text, const D2D1_RECT_F& rect, IDWriteTextFormat* format) {
    if (!d2d_context_ || !brush_text_ || !format) {
        return;
    }
    d2d_context_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush_text_.Get());
}

void QmiApp::DrawButtonGlyph(TitleButton button, const RECT& rect) {
    if (!d2d_context_ || !brush_text_) {
        return;
    }

    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float w = static_cast<float>(rect.right - rect.left);
    const float half = std::max(7.5f, w * 0.155f);
    const float stroke = 1.3f;
    const float cross_half = half * 0.88f;
    auto snap = [](float value) { return std::floor(value) + 0.5f; };

    const D2D1_ANTIALIAS_MODE previous_aa = d2d_context_->GetAntialiasMode();
    d2d_context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    switch (button) {
        case TitleButton::Minimize: {
            const float left = snap(cx - half);
            const float right = snap(cx + half);
            const float y = snap(cy + half * 0.46f);
            d2d_context_->DrawLine(D2D1::Point2F(left, y),
                                   D2D1::Point2F(right, y),
                                   brush_text_.Get(),
                                   stroke);
            break;
        }
        case TitleButton::Maximize: {
            const bool maximized = IsZoomed(hwnd_) != FALSE;
            if (!maximized) {
                const float left = snap(cx - half);
                const float top = snap(cy - half);
                const float right = snap(cx + half);
                const float bottom = snap(cy + half);
                d2d_context_->DrawRectangle(D2D1::RectF(left, top, right, bottom), brush_text_.Get(), stroke);
            } else {
                const float offset_x = 2.5f;
                const float offset_y = 2.0f;

                const float back_left = snap(cx - half + offset_x);
                const float back_top = snap(cy - half - offset_y);
                const float back_right = snap(cx + half + offset_x);
                const float back_bottom = snap(cy + half - offset_y);

                const float front_left = snap(cx - half - offset_x);
                const float front_top = snap(cy - half + offset_y);
                const float front_right = snap(cx + half - offset_x);
                const float front_bottom = snap(cy + half + offset_y);

                // Draw only the visible parts of the back window so the front window naturally occludes it.
                d2d_context_->DrawLine(D2D1::Point2F(back_left, back_top),
                                       D2D1::Point2F(back_right, back_top),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(back_right, back_top),
                                       D2D1::Point2F(back_right, back_bottom),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(back_right, back_bottom),
                                       D2D1::Point2F(front_right, back_bottom),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(back_left, back_top),
                                       D2D1::Point2F(back_left, front_top),
                                       brush_text_.Get(),
                                       stroke);

                d2d_context_->DrawLine(D2D1::Point2F(front_left, front_top),
                                       D2D1::Point2F(front_right, front_top),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(front_left, front_top),
                                       D2D1::Point2F(front_left, front_bottom),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(front_right, front_top),
                                       D2D1::Point2F(front_right, front_bottom),
                                       brush_text_.Get(),
                                       stroke);
                d2d_context_->DrawLine(D2D1::Point2F(front_left, front_bottom),
                                       D2D1::Point2F(front_right, front_bottom),
                                       brush_text_.Get(),
                                       stroke);
            }
            break;
        }
        case TitleButton::Close: {
            d2d_context_->DrawLine(D2D1::Point2F(snap(cx - cross_half), snap(cy - cross_half)),
                                   D2D1::Point2F(snap(cx + cross_half), snap(cy + cross_half)),
                                   brush_text_.Get(),
                                   stroke);
            d2d_context_->DrawLine(D2D1::Point2F(snap(cx + cross_half), snap(cy - cross_half)),
                                   D2D1::Point2F(snap(cx - cross_half), snap(cy + cross_half)),
                                   brush_text_.Get(),
                                   stroke);
            break;
        }
        default:
            break;
    }

    d2d_context_->SetAntialiasMode(previous_aa);
}

void QmiApp::DrawTopInfoBar(const TitleButtons& buttons) {
    if (!d2d_context_ || !brush_text_ || !info_text_format_ || current_image_info_.empty()) {
        return;
    }

    const float left = 10.0f;
    const float top = static_cast<float>(buttons.min_rect.top);
    const float right = static_cast<float>(buttons.min_rect.left) - 8.0f;
    const float bottom = static_cast<float>(buttons.min_rect.bottom);
    if (right <= left + 24.0f) {
        return;
    }

    const D2D1_RECT_F panel_rect = D2D1::RectF(left, top, right, bottom);
    const D2D1_RECT_F text_rect = D2D1::RectF(panel_rect.left + 10.0f, panel_rect.top, panel_rect.right - 10.0f, panel_rect.bottom);
    std::wstring info_text = current_image_info_;
    if (IsRenderableImageType(current_image_.type)) {
        const D2D1_SIZE_F size = d2d_context_->GetSize();
        const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
        const float current_scale = std::max(0.02f, GetBaseImageScale(viewport) * zoom_);
        info_text += L" | ";
        info_text += FormatZoomPercentText(current_scale);
    } else {
        info_text += L" | \u7f29\u653e -";
    }

    d2d_context_->DrawTextW(info_text.c_str(),
                            static_cast<UINT32>(info_text.size()),
                            info_text_format_.Get(),
                            text_rect,
                            brush_text_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void QmiApp::DrawTitleButtons(const TitleButtons& buttons) {
    if (!d2d_context_ || !brush_hover_ || !brush_close_hover_) {
        return;
    }

    auto draw_button = [&](TitleButton button, const RECT& rect) {
        D2D1_RECT_F d2d_rect = D2D1::RectF(
            static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right), static_cast<float>(rect.bottom));

        if (hover_button_ == button) {
            d2d_context_->FillRectangle(d2d_rect, button == TitleButton::Close ? brush_close_hover_.Get() : brush_hover_.Get());
        }
        DrawButtonGlyph(button, rect);
    };

    draw_button(TitleButton::Minimize, buttons.min_rect);
    draw_button(TitleButton::Maximize, buttons.max_rect);
    draw_button(TitleButton::Close, buttons.close_rect);
}

void QmiApp::DrawEdgeNavButtons(const D2D1_RECT_F& viewport) {
    if (!d2d_context_ || !brush_panel_ || !brush_overlay_ || !brush_text_ || current_image_.type == ImageType::None || images_.size() <= 1) {
        return;
    }

    EdgeNavButton button = visible_edge_nav_button_;
    if (pressed_edge_nav_button_ != EdgeNavButton::None) {
        button = pressed_edge_nav_button_;
    }
    if (button == EdgeNavButton::None) {
        return;
    }

    const EdgeNavButtons buttons = GetEdgeNavButtons(viewport);
    const D2D1_RECT_F rect = (button == EdgeNavButton::Previous) ? buttons.prev_rect : buttons.next_rect;
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float radius = (rect.right - rect.left) * 0.5f;
    const D2D1_ELLIPSE circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);

    d2d_context_->FillEllipse(circle, brush_panel_.Get());
    if (hover_edge_nav_button_ == button && brush_hover_) {
        d2d_context_->FillEllipse(circle, brush_hover_.Get());
    }

    if (pressed_edge_nav_button_ == button && brush_accent_) {
        d2d_context_->DrawEllipse(circle, brush_accent_.Get(), 2.0f);
    } else {
        d2d_context_->DrawEllipse(circle, brush_overlay_.Get(), 1.0f);
    }

    const float chevron_half = radius * 0.34f;
    const float notch = radius * 0.24f;
    const float stroke = 2.0f;
    if (button == EdgeNavButton::Previous) {
        d2d_context_->DrawLine(D2D1::Point2F(cx + notch, cy - chevron_half), D2D1::Point2F(cx - notch, cy), brush_text_.Get(), stroke);
        d2d_context_->DrawLine(D2D1::Point2F(cx - notch, cy), D2D1::Point2F(cx + notch, cy + chevron_half), brush_text_.Get(), stroke);
    } else {
        d2d_context_->DrawLine(D2D1::Point2F(cx - notch, cy - chevron_half), D2D1::Point2F(cx + notch, cy), brush_text_.Get(), stroke);
        d2d_context_->DrawLine(D2D1::Point2F(cx + notch, cy), D2D1::Point2F(cx - notch, cy + chevron_half), brush_text_.Get(), stroke);
    }
}

void QmiApp::DrawOpenButton(const D2D1_RECT_F& viewport) {
    if (!d2d_context_ || !brush_open_button_fill_ || !brush_open_button_stroke_ || !brush_text_) {
        return;
    }
    if (current_image_.type != ImageType::None) {
        return;
    }

    const D2D1_RECT_F button_rect = GetOpenButtonRect(viewport);
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(button_rect, 10.0f, 10.0f);

    d2d_context_->FillRoundedRectangle(rounded, brush_open_button_fill_.Get());
    if (hover_open_button_ && brush_open_button_hover_) {
        d2d_context_->FillRoundedRectangle(rounded, brush_open_button_hover_.Get());
    }

    if (pressed_open_button_ && brush_accent_) {
        d2d_context_->DrawRoundedRectangle(rounded, brush_accent_.Get(), 2.0f);
    } else if (brush_open_button_stroke_) {
        d2d_context_->DrawRoundedRectangle(rounded, brush_open_button_stroke_.Get(), 1.0f);
    }

    DrawCenteredText(L"\u6253\u5f00\u56fe\u7247", button_rect, text_format_.Get());
}

void QmiApp::DrawBrokenImagePlaceholder(const D2D1_RECT_F& viewport) {
    if (!d2d_context_ || !brush_panel_ || !brush_overlay_ || !brush_text_) {
        return;
    }

    const float view_w = std::max(1.0f, viewport.right - viewport.left);
    const float view_h = std::max(1.0f, viewport.bottom - viewport.top);
    const float icon_w = Clamp(std::min(view_w, view_h) * 0.34f, 96.0f, 190.0f);
    const float icon_h = icon_w * 0.72f;
    const float cx = (viewport.left + viewport.right) * 0.5f;
    const float cy = (viewport.top + viewport.bottom) * 0.5f - 12.0f;

    const D2D1_RECT_F rect =
        D2D1::RectF(cx - icon_w * 0.5f, cy - icon_h * 0.5f, cx + icon_w * 0.5f, cy + icon_h * 0.5f);
    const D2D1_ROUNDED_RECT frame = D2D1::RoundedRect(rect, 10.0f, 10.0f);
    d2d_context_->FillRoundedRectangle(frame, brush_panel_.Get());
    d2d_context_->DrawRoundedRectangle(frame, brush_overlay_.Get(), 2.0f);

    const float dot_r = std::max(4.0f, icon_w * 0.045f);
    d2d_context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + icon_w * 0.2f, rect.top + icon_h * 0.24f), dot_r, dot_r),
                              brush_text_.Get());

    d2d_context_->DrawLine(D2D1::Point2F(rect.left + icon_w * 0.16f, rect.bottom - icon_h * 0.18f),
                           D2D1::Point2F(rect.left + icon_w * 0.40f, rect.top + icon_h * 0.55f),
                           brush_text_.Get(),
                           1.8f);
    d2d_context_->DrawLine(D2D1::Point2F(rect.left + icon_w * 0.40f, rect.top + icon_h * 0.55f),
                           D2D1::Point2F(rect.left + icon_w * 0.58f, rect.bottom - icon_h * 0.30f),
                           brush_text_.Get(),
                           1.8f);
    d2d_context_->DrawLine(D2D1::Point2F(rect.left + icon_w * 0.58f, rect.bottom - icon_h * 0.30f),
                           D2D1::Point2F(rect.right - icon_w * 0.16f, rect.bottom - icon_h * 0.20f),
                           brush_text_.Get(),
                           1.8f);

    const float crack_top = rect.top + icon_h * 0.08f;
    const float crack_bottom = rect.bottom - icon_h * 0.08f;
    const float crack_x = cx + icon_w * 0.06f;
    d2d_context_->DrawLine(D2D1::Point2F(crack_x - 8.0f, crack_top + 3.0f),
                           D2D1::Point2F(crack_x + 4.0f, crack_top + 24.0f),
                           brush_text_.Get(),
                           2.1f);
    d2d_context_->DrawLine(D2D1::Point2F(crack_x + 4.0f, crack_top + 24.0f),
                           D2D1::Point2F(crack_x - 6.0f, crack_top + 46.0f),
                           brush_text_.Get(),
                           2.1f);
    d2d_context_->DrawLine(D2D1::Point2F(crack_x - 6.0f, crack_top + 46.0f),
                           D2D1::Point2F(crack_x + 5.0f, crack_bottom - 12.0f),
                           brush_text_.Get(),
                           2.1f);
    d2d_context_->DrawLine(D2D1::Point2F(crack_x + 5.0f, crack_bottom - 12.0f),
                           D2D1::Point2F(crack_x - 2.0f, crack_bottom),
                           brush_text_.Get(),
                           2.1f);

    const float text_top = rect.bottom + 12.0f;
    DrawCenteredText(L"\u56fe\u7247\u5df2\u635f\u574f\u6216\u65e0\u6cd5\u89e3\u7801",
                     D2D1::RectF(viewport.left + 20.0f, text_top, viewport.right - 20.0f, text_top + 28.0f),
                     small_text_format_.Get());
}

void QmiApp::DrawImageRegion(const D2D1_RECT_F& viewport) {
    if (!d2d_context_) {
        return;
    }
    if (brush_viewport_bg_) {
        // Per-pixel layered windows treat alpha=0 as hit-test transparent; keep viewport letterbox semi-transparent.
        d2d_context_->FillRectangle(viewport, brush_viewport_bg_.Get());
    }

    if (current_image_.type == ImageType::None) {
        DrawOpenButton(viewport);
        return;
    }
    if (current_image_.type == ImageType::Broken) {
        DrawBrokenImagePlaceholder(viewport);
        return;
    }

    const D2D1_RECT_F dest = GetImageDestinationRect(viewport);
    const D2D1_SIZE_F transformed_size = GetTransformedImageSize();
    const float transformed_w = std::max(1.0f, transformed_size.width);
    const float transformed_h = std::max(1.0f, transformed_size.height);
    const float dest_w = std::max(1.0f, dest.right - dest.left);
    const float dest_h = std::max(1.0f, dest.bottom - dest.top);
    const float scale = std::min(dest_w / transformed_w, dest_h / transformed_h);
    const float source_w = std::max(1.0f, current_image_.width);
    const float source_h = std::max(1.0f, current_image_.height);
    const float center_x = (dest.left + dest.right) * 0.5f;
    const float center_y = (dest.top + dest.bottom) * 0.5f;
    const float draw_w = source_w * scale;
    const float draw_h = source_h * scale;
    const D2D1_RECT_F draw_rect =
        D2D1::RectF(center_x - draw_w * 0.5f, center_y - draw_h * 0.5f, center_x + draw_w * 0.5f, center_y + draw_h * 0.5f);

    d2d_context_->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const D2D1_POINT_2F center = D2D1::Point2F(center_x, center_y);
    const D2D1_MATRIX_3X2_F image_transform = GetImageTransformMatrix(center);

    if (current_image_.type == ImageType::Raster && current_image_.raster) {
        D2D1_MATRIX_3X2_F prev{};
        d2d_context_->GetTransform(&prev);
        d2d_context_->SetTransform(image_transform);
        const D2D1_INTERPOLATION_MODE interp =
            smooth_sampling_ ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        d2d_context_->DrawBitmap(current_image_.raster.Get(), draw_rect, 1.0f, interp);
        d2d_context_->SetTransform(prev);
    } else if (current_image_.type == ImageType::Svg && current_image_.svg && d2d_context5_) {
        D2D1_MATRIX_3X2_F prev{};
        d2d_context_->GetTransform(&prev);
        d2d_context_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(0.0f, 0.0f)) *
                                   D2D1::Matrix3x2F::Translation(draw_rect.left, draw_rect.top) * image_transform);
        d2d_context5_->DrawSvgDocument(current_image_.svg.Get());
        d2d_context_->SetTransform(prev);
    }

    d2d_context_->PopAxisAlignedClip();
}

void QmiApp::DrawFilmStrip(const D2D1_RECT_F& strip_rect) {
    if (!d2d_context_ || !brush_viewport_bg_ || !brush_thumb_bg_) {
        return;
    }

    d2d_context_->FillRectangle(strip_rect, brush_viewport_bg_.Get());

    visible_thumbs_.clear();
    if (images_.empty()) {
        return;
    }
    if (thumbnail_draw_scales_.size() != images_.size()) {
        thumbnail_draw_scales_.assign(images_.size(), 0.0f);
    }

    const float padding_x = 12.0f;
    const float padding_y = 8.0f;
    const float gap = 10.0f;
    const float strip_width = std::max(0.0f, strip_rect.right - strip_rect.left);
    const float available_h = std::max(40.0f, (strip_rect.bottom - strip_rect.top) - 2.0f * padding_y);
    const float medium_item_h = std::max(36.0f, available_h * 0.92f);
    const float medium_item_w = medium_item_h * 1.3f;
    const int image_count = static_cast<int>(images_.size());
    const float side_hover_reserve = Clamp(medium_item_w * kFilmStripHoverScaleBoost * 0.9f, 8.0f, 24.0f);
    const float horizontal_padding = padding_x + side_hover_reserve;
    const float inner_left = strip_rect.left + horizontal_padding;
    const float inner_width = std::max(0.0f, strip_width - horizontal_padding * 2.0f);
    const float baseline = strip_rect.bottom - padding_y;

    auto compute_target_scale = [&](int index) {
        float target_scale = kFilmStripSmallScale;
        if (current_index_ < 0) {
            target_scale = kFilmStripMediumScale;
        } else {
            const int index_distance = std::abs(index - current_index_);
            if (index_distance == 0) {
                target_scale = kFilmStripLargeScale;
            } else if (index_distance == 1) {
                target_scale = kFilmStripMediumScale;
            }
        }
        if (index == hover_thumbnail_index_) {
            target_scale += kFilmStripHoverScaleBoost;
        }
        return target_scale;
    };

    std::vector<float> target_scales(image_count, kFilmStripSmallScale);
    std::vector<float> layout_widths(image_count, medium_item_w * kFilmStripSmallScale);
    for (int i = 0; i < image_count; ++i) {
        const float target_scale = compute_target_scale(i);
        target_scales[i] = target_scale;
        const float previous_scale = thumbnail_draw_scales_[i];
        const float layout_scale = (previous_scale > 0.0f) ? std::max(previous_scale, target_scale) : target_scale;
        layout_widths[i] = medium_item_w * layout_scale;
    }

    auto fits_width = [&](float content_width, float candidate_width) {
        const float next_width = content_width + ((content_width > 0.0f) ? gap : 0.0f) + candidate_width;
        return next_width <= inner_width + 0.01f;
    };

    int start = 0;
    int end = 0;
    float packed_width = 0.0f;

    if (film_strip_scroll_index_ >= 0) {
        start = Clamp(film_strip_scroll_index_, 0, image_count - 1);
        end = start;
        while (end < image_count && (end == start || fits_width(packed_width, layout_widths[end]))) {
            packed_width += ((packed_width > 0.0f) ? gap : 0.0f) + layout_widths[end];
            ++end;
        }
    } else if (current_index_ >= 0) {
        const int anchor = Clamp(current_index_, 0, image_count - 1);
        start = anchor;
        end = anchor + 1;
        const float anchor_width = layout_widths[anchor];
        const float side_capacity = std::max(0.0f, inner_width * 0.5f - anchor_width * 0.5f);
        float used_left = 0.0f;
        for (int i = anchor - 1; i >= 0; --i) {
            const float needed = gap + layout_widths[i];
            if (used_left + needed > side_capacity + 0.01f) {
                break;
            }
            used_left += needed;
            start = i;
        }
        float used_right = 0.0f;
        for (int i = anchor + 1; i < image_count; ++i) {
            const float needed = gap + layout_widths[i];
            if (used_right + needed > side_capacity + 0.01f) {
                break;
            }
            used_right += needed;
            end = i + 1;
        }
    } else {
        while (end < image_count && (end == start || fits_width(packed_width, layout_widths[end]))) {
            packed_width += ((packed_width > 0.0f) ? gap : 0.0f) + layout_widths[end];
            ++end;
        }
    }

    if (end <= start) {
        start = Clamp((current_index_ >= 0) ? current_index_ : 0, 0, image_count - 1);
        end = std::min(image_count, start + 1);
    }

    const int visible_count = std::max(0, end - start);
    bool has_pending_scale_animation = false;

    std::vector<float> draw_scales;
    std::vector<float> draw_widths;
    draw_scales.reserve(visible_count);
    draw_widths.reserve(visible_count);
    for (int i = start; i < end; ++i) {
        const float target_scale = target_scales[i];
        float draw_scale = target_scale;
        const float previous_scale = thumbnail_draw_scales_[i];
        if (previous_scale > 0.0f) {
            draw_scale = previous_scale + (target_scale - previous_scale) * kFilmStripScaleLerp;
            if (std::fabs(target_scale - draw_scale) < 0.01f) {
                draw_scale = target_scale;
            } else {
                has_pending_scale_animation = true;
            }
        }
        thumbnail_draw_scales_[i] = draw_scale;
        draw_scales.push_back(draw_scale);
        draw_widths.push_back(medium_item_w * draw_scale);
    }

    float content_width = 0.0f;
    for (size_t i = 0; i < draw_widths.size(); ++i) {
        content_width += draw_widths[i];
        if (i + 1 < draw_widths.size()) {
            content_width += gap;
        }
    }
    float x = inner_left + std::max(0.0f, (inner_width - content_width) * 0.5f);
    if (film_strip_scroll_index_ < 0 && current_index_ >= start && current_index_ < end && !draw_widths.empty()) {
        const int anchor = current_index_;
        float width_before_anchor = 0.0f;
        for (int i = start; i < anchor; ++i) {
            const size_t layout_index = static_cast<size_t>(i - start);
            width_before_anchor += draw_widths[layout_index] + gap;
        }
        const float anchor_width = draw_widths[static_cast<size_t>(anchor - start)];
        const float center_x = inner_left + inner_width * 0.5f;
        x = center_x - (width_before_anchor + anchor_width * 0.5f);
        const float min_x = inner_left;
        const float max_x = inner_left + std::max(0.0f, inner_width - content_width);
        x = Clamp(x, min_x, max_x);
    }
    int remaining_decode_budget = kThumbnailDecodeBudgetPerFrame;
    bool has_pending_visible_thumbnail = false;
    bool has_hovered_visible_thumb = false;
    D2D1_RECT_F hovered_thumb_rect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

    if (current_index_ >= start && current_index_ < end && current_index_ < static_cast<int>(thumbnails_.size()) &&
        !thumbnails_[current_index_].attempted && remaining_decode_budget > 0) {
        EnsureThumbnailLoaded(current_index_);
        --remaining_decode_budget;
    }

    for (int i = start; i < end; ++i) {
        const size_t layout_index = static_cast<size_t>(i - start);
        const float draw_scale = draw_scales[layout_index];
        const float item_h = medium_item_h * draw_scale;
        const float item_w = draw_widths[layout_index];
        const float item_top = std::max(strip_rect.top + padding_y, baseline - item_h);
        D2D1_RECT_F cell = D2D1::RectF(x, item_top, x + item_w, baseline);
        visible_thumbs_.push_back(VisibleThumb{i, cell});
        if (i == hover_thumbnail_index_) {
            has_hovered_visible_thumb = true;
            hovered_thumb_rect = cell;
        }

        if (i != current_index_ && i < static_cast<int>(thumbnails_.size()) && !thumbnails_[i].attempted) {
            if (remaining_decode_budget > 0) {
                EnsureThumbnailLoaded(i);
                --remaining_decode_budget;
            } else {
                has_pending_visible_thumbnail = true;
            }
        }
        const Thumbnail& thumb = thumbnails_[i];
        d2d_context_->FillRectangle(cell, brush_thumb_bg_.Get());

        if (thumb.bitmap) {
            const float scale = std::min((cell.right - cell.left) / thumb.width, (cell.bottom - cell.top) / thumb.height);
            const float draw_w = thumb.width * scale;
            const float draw_h = thumb.height * scale;
            const float cx = (cell.left + cell.right) * 0.5f;
            const float cy = (cell.top + cell.bottom) * 0.5f;
            const D2D1_RECT_F dst =
                D2D1::RectF(cx - draw_w * 0.5f, cy - draw_h * 0.5f, cx + draw_w * 0.5f, cy + draw_h * 0.5f);
            d2d_context_->DrawBitmap(thumb.bitmap.Get(), dst, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        } else {
            const std::wstring ext = ToLower(images_[i].extension().wstring());
            std::wstring label = ext.empty() ? L"\u56fe\u7247" : ext.substr(1);
            DrawCenteredText(label, cell, small_text_format_.Get());
        }

        const float stroke = (i == current_index_) ? 3.0f : 1.0f;
        ID2D1SolidColorBrush* stroke_brush = (i == current_index_) ? brush_accent_.Get() : brush_overlay_.Get();
        d2d_context_->DrawRectangle(cell, stroke_brush, stroke);

        x += item_w + gap;
    }

    if (has_hovered_visible_thumb && hover_thumbnail_index_ >= 0 && hover_thumbnail_index_ < image_count && brush_text_ &&
        small_text_format_) {
        const std::wstring hover_name = images_[hover_thumbnail_index_].filename().wstring();
        if (!hover_name.empty()) {
            const float text_h = 22.0f;
            const float text_margin = 6.0f;
            const float text_gap = 4.0f;
            float text_w = std::min(std::max(120.0f, (hovered_thumb_rect.right - hovered_thumb_rect.left) * 1.8f),
                                    strip_width - text_margin * 2.0f);
            text_w = std::max(60.0f, text_w);
            const float center_x = (hovered_thumb_rect.left + hovered_thumb_rect.right) * 0.5f;
            const float min_left = strip_rect.left + text_margin;
            const float max_left = std::max(min_left, strip_rect.right - text_margin - text_w);
            const float text_left = Clamp(center_x - text_w * 0.5f, min_left, max_left);
            const float text_bottom = hovered_thumb_rect.top - text_gap;
            const float text_top = std::max(2.0f, text_bottom - text_h);
            const D2D1_RECT_F text_rect = D2D1::RectF(text_left, text_top, text_left + text_w, text_bottom);
            d2d_context_->DrawTextW(hover_name.c_str(),
                                    static_cast<UINT32>(hover_name.size()),
                                    small_text_format_.Get(),
                                    text_rect,
                                    brush_text_.Get(),
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    if (has_pending_visible_thumbnail || has_pending_scale_animation) {
        RequestRender(true);
    }
}

bool QmiApp::PresentLayeredFrame() {
    if (!hwnd_ || !d3d_context_ || !frame_texture_ || !readback_texture_ || !layered_dc_ || !layered_bits_) {
        return false;
    }
    if (layered_width_ == 0 || layered_height_ == 0 || layered_stride_ == 0) {
        return false;
    }

    d3d_context_->CopyResource(readback_texture_.Get(), frame_texture_.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = d3d_context_->Map(readback_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        DiscardDeviceResources();
        return false;
    }
    if (FAILED(hr)) {
        return false;
    }

    auto* dst = static_cast<std::uint8_t*>(layered_bits_);
    auto* src = static_cast<const std::uint8_t*>(mapped.pData);
    const UINT copy_width = layered_width_ * 4;
    for (UINT y = 0; y < layered_height_; ++y) {
        memcpy(dst + static_cast<size_t>(y) * layered_stride_, src + static_cast<size_t>(y) * mapped.RowPitch, copy_width);
    }
    d3d_context_->Unmap(readback_texture_.Get(), 0);

    RECT wnd_rect{};
    GetWindowRect(hwnd_, &wnd_rect);
    POINT dst_pt{wnd_rect.left, wnd_rect.top};
    POINT src_pt{0, 0};
    SIZE size{static_cast<LONG>(layered_width_), static_cast<LONG>(layered_height_)};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    return UpdateLayeredWindow(hwnd_, nullptr, &dst_pt, &size, layered_dc_, &src_pt, 0, &blend, ULW_ALPHA) != FALSE;
}

void QmiApp::Render() {
    if (!InitDeviceResources() || !d2d_context_ || !frame_texture_ || !readback_texture_) {
        return;
    }

    D2D1_SIZE_F size = d2d_context_->GetSize();
    const D2D1_RECT_F strip = GetFilmStripRect(size.width, size.height);
    const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);

    d2d_context_->BeginDraw();
    d2d_context_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d_context_->Clear(D2D1::ColorF(0x000000, 0.0f));

    DrawImageRegion(viewport);
    DrawEdgeNavButtons(viewport);

    if (brush_overlay_) {
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, viewport.top), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, viewport.top, viewport.left, viewport.bottom), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(viewport.right, viewport.top, size.width, viewport.bottom), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, viewport.bottom, size.width, strip.top), brush_overlay_.Get());
    }

    DrawFilmStrip(strip);
    const TitleButtons title_buttons = GetTitleButtons();
    DrawTopInfoBar(title_buttons);
    DrawTitleButtons(title_buttons);

    HRESULT hr = d2d_context_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (FAILED(hr)) {
        return;
    }
    if (!PresentLayeredFrame()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void QmiApp::OpenSettingsWindow() {
    if (settings_hwnd_ && IsWindow(settings_hwnd_)) {
        ShowWindow(settings_hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(settings_hwnd_);
        return;
    }

    const RECT work_area = GetMonitorWorkAreaFromWindow(hwnd_);
    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const int x = work_area.left + std::max(0, (work_width - kSettingsWindowWidth) / 2);
    const int y = work_area.top + std::max(0, (work_height - kSettingsWindowHeight) / 2);

    settings_hwnd_ = CreateWindowExW(WS_EX_APPWINDOW,
                                     kSettingsClassName,
                                     L"Qmi \u8bbe\u7f6e",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                     x,
                                     y,
                                     kSettingsWindowWidth,
                                     kSettingsWindowHeight,
                                     hwnd_,
                                     nullptr,
                                     hinstance_,
                                     this);
    if (settings_hwnd_) {
        ShowWindow(settings_hwnd_, SW_SHOWNORMAL);
        UpdateWindow(settings_hwnd_);
    }
}

void QmiApp::RequestRender(bool interactive) {
    if (!hwnd_) {
        return;
    }
    if (!interactive) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG elapsed = now - last_interactive_render_tick_;
    if (elapsed >= kInteractiveFrameIntervalMs) {
        last_interactive_render_tick_ = now;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (!render_timer_armed_) {
        const UINT delay = static_cast<UINT>(kInteractiveFrameIntervalMs - elapsed);
        SetTimer(hwnd_, kRenderTimerId, std::max<UINT>(1u, delay), nullptr);
        render_timer_armed_ = true;
    }
}

LRESULT QmiApp::HitTestNonClient(POINT screen_pt) const {
    if (!hwnd_) {
        return HTCLIENT;
    }

    RECT window_rect{};
    GetWindowRect(hwnd_, &window_rect);

    if (!IsZoomed(hwnd_)) {
        const bool on_left = screen_pt.x >= window_rect.left && screen_pt.x < window_rect.left + kResizeBorder;
        const bool on_right = screen_pt.x < window_rect.right && screen_pt.x >= window_rect.right - kResizeBorder;
        const bool on_top = screen_pt.y >= window_rect.top && screen_pt.y < window_rect.top + kResizeBorder;
        const bool on_bottom = screen_pt.y < window_rect.bottom && screen_pt.y >= window_rect.bottom - kResizeBorder;

        if (on_top && on_left) {
            return HTTOPLEFT;
        }
        if (on_top && on_right) {
            return HTTOPRIGHT;
        }
        if (on_bottom && on_left) {
            return HTBOTTOMLEFT;
        }
        if (on_bottom && on_right) {
            return HTBOTTOMRIGHT;
        }
        if (on_left) {
            return HTLEFT;
        }
        if (on_right) {
            return HTRIGHT;
        }
        if (on_top) {
            return HTTOP;
        }
        if (on_bottom) {
            return HTBOTTOM;
        }
    }

    POINT client_pt = screen_pt;
    ScreenToClient(hwnd_, &client_pt);
    if (HitTestTitleButton(client_pt) != TitleButton::None) {
        return HTCLIENT;
    }

    return HTCLIENT;
}

LRESULT QmiApp::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_NCCALCSIZE:
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
            mmi->ptMinTrackSize.x = kMinWindowWidth;
            mmi->ptMinTrackSize.y = kMinWindowHeight;

            HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(monitor, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            return HitTestNonClient(pt);
        }

        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED && d2d_context_) {
                CreateWindowSizeResources();
            }
            RequestRender();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            Render();
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            TrackMouseEvent(&tme);

            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            UpdateHoverState(pt);

            if (dragging_image_) {
                const int dx = pt.x - drag_last_.x;
                const int dy = pt.y - drag_last_.y;
                pan_x_ += static_cast<float>(dx);
                pan_y_ += static_cast<float>(dy);
                drag_last_ = pt;
                RequestRender(true);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            hover_button_ = TitleButton::None;
            hover_open_button_ = false;
            hover_edge_nav_button_ = EdgeNavButton::None;
            visible_edge_nav_button_ = EdgeNavButton::None;
            hover_thumbnail_index_ = -1;
            RequestRender();
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

            const TitleButton button = HitTestTitleButton(pt);
            if (button != TitleButton::None) {
                pressed_button_ = button;
                SetCapture(hwnd_);
                RequestRender();
                return 0;
            }

            if (HitTestOpenButton(pt)) {
                pressed_open_button_ = true;
                SetCapture(hwnd_);
                RequestRender();
                return 0;
            }

            const EdgeNavButton nav_button = HitTestEdgeNavButton(pt);
            if (nav_button != EdgeNavButton::None) {
                pressed_edge_nav_button_ = nav_button;
                SetCapture(hwnd_);
                RequestRender();
                return 0;
            }

            const int thumb_index = HitTestThumbnail(pt);
            if (thumb_index >= 0 && thumb_index < static_cast<int>(images_.size())) {
                LoadImageByIndex(thumb_index, true);
                return 0;
            }

            bool point_over_visible_image = false;
            if (d2d_context_) {
                const D2D1_SIZE_F size = d2d_context_->GetSize();
                const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
                point_over_visible_image = IsPointOverVisibleImage(pt, viewport);
            }

            if (point_over_visible_image) {
                dragging_image_ = true;
                drag_last_ = pt;
                SetCapture(hwnd_);
            } else {
                POINT screen_pt = pt;
                ClientToScreen(hwnd_, &screen_pt);
                ReleaseCapture();
                SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen_pt.x, screen_pt.y));
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (pressed_button_ != TitleButton::None) {
                const TitleButton released_on = HitTestTitleButton(pt);
                const TitleButton pressed = pressed_button_;
                pressed_button_ = TitleButton::None;
                ReleaseCapture();
                RequestRender();

                if (released_on == pressed) {
                    if (pressed == TitleButton::Close) {
                        DestroyWindow(hwnd_);
                    } else if (pressed == TitleButton::Minimize) {
                        ShowWindow(hwnd_, SW_MINIMIZE);
                    } else if (pressed == TitleButton::Maximize) {
                        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
                    }
                }
                return 0;
            }

            if (pressed_open_button_) {
                const bool released_on_button = HitTestOpenButton(pt);
                pressed_open_button_ = false;
                ReleaseCapture();
                RequestRender();
                if (released_on_button) {
                    OpenFileDialog();
                }
                return 0;
            }

            if (pressed_edge_nav_button_ != EdgeNavButton::None) {
                const EdgeNavButton released_on = HitTestEdgeNavButton(pt);
                const EdgeNavButton pressed = pressed_edge_nav_button_;
                pressed_edge_nav_button_ = EdgeNavButton::None;
                ReleaseCapture();
                RequestRender();
                if (released_on == pressed) {
                    MoveSelection(pressed == EdgeNavButton::Previous ? -1 : 1);
                }
                return 0;
            }

            if (dragging_image_) {
                dragging_image_ = false;
                ReleaseCapture();
                return 0;
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam), pt);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

            if (HitTestTitleButton(pt) != TitleButton::None || HitTestOpenButton(pt) ||
                HitTestEdgeNavButton(pt) != EdgeNavButton::None) {
                return 0;
            }

            const int thumb_index = HitTestThumbnail(pt);
            if (thumb_index >= 0 && thumb_index < static_cast<int>(images_.size())) {
                return 0;
            }

            bool point_over_visible_image = false;
            if (d2d_context_) {
                const D2D1_SIZE_F size = d2d_context_->GetSize();
                const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
                point_over_visible_image = IsPointOverVisibleImage(pt, viewport);
            }

            if (point_over_visible_image) {
                HandleImageDoubleClick(pt);
            } else {
                ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            POINT pt = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (pt.x == -1 && pt.y == -1) {
                RECT rc{};
                GetClientRect(hwnd_, &rc);
                pt.x = rc.left + 40;
                pt.y = rc.top + 40;
                ClientToScreen(hwnd_, &pt);
            }
            ShowContextMenu(pt);
            return 0;
        }

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wparam);
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
                OpenImagePath(path, true);
            }
            DragFinish(drop);
            return 0;
        }

        case WM_KEYDOWN: {
            const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl_down && wparam == 'C') {
                if (IsRenderableImageType(current_image_.type)) {
                    CopyCurrentImageToClipboard();
                }
            } else if (ctrl_down && wparam == 'P') {
                if (!current_image_.path.empty() && !PrintCurrentImage()) {
                    MessageBoxW(hwnd_,
                                L"\u6253\u5370\u56fe\u7247\u5931\u8d25\uff0c\u8bf7\u786e\u8ba4\u7cfb\u7edf\u5df2\u914d\u7f6e\u6253\u5370\u673a\uff0c\u5e76\u5177\u6709\u53ef\u7528\u7684\u9ed8\u8ba4\u6253\u5370\u7a0b\u5e8f\u3002",
                                L"Qmi",
                                MB_ICONERROR | MB_OK);
                }
            } else if (wparam == VK_DELETE) {
                if (!current_image_.path.empty() && !MoveCurrentFileToRecycleBin()) {
                    MessageBoxW(hwnd_,
                                L"\u5220\u9664\u6587\u4ef6\u5931\u8d25\uff0c\u6587\u4ef6\u53ef\u80fd\u4e0d\u5b58\u5728\u6216\u65e0\u6cd5\u79fb\u5165\u56de\u6536\u7ad9\u3002",
                                L"Qmi",
                                MB_ICONERROR | MB_OK);
                }
            } else if (wparam == VK_RIGHT) {
                MoveSelection(1);
            } else if (wparam == VK_LEFT) {
                MoveSelection(-1);
            } else if (wparam == VK_ESCAPE) {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            } else if (wparam == VK_UP || wparam == VK_DOWN) {
                RECT client_rect{};
                if (GetClientRect(hwnd_, &client_rect)) {
                    POINT anchor_pt = POINT{(client_rect.left + client_rect.right) / 2,
                                            (client_rect.top + client_rect.bottom) / 2};
                    ClientToScreen(hwnd_, &anchor_pt);
                    HandleMouseWheel((wparam == VK_UP) ? WHEEL_DELTA : -WHEEL_DELTA, anchor_pt);
                }
            } else if (wparam == '0') {
                ResetView();
                RequestRender();
            }
            return 0;
        }

        case WM_TIMER:
            if (wparam == kAnimationTimerId) {
                KillTimer(hwnd_, kAnimationTimerId);
                if (!animation_decoder_ || animation_frame_delays_ms_.size() <= 1 || current_image_.type != ImageType::Raster) {
                    return 0;
                }
                const size_t frame_count = animation_frame_delays_ms_.size();
                const size_t next_index = (animation_frame_index_ + 1) % frame_count;

                ComPtr<ID2D1Bitmap1> next_frame;
                if (FAILED(DecodeGifFrame(next_index, &next_frame)) || !next_frame) {
                    ClearAnimationState();
                    return 0;
                }

                animation_frame_index_ = next_index;
                current_image_.raster = next_frame;
                RequestRender();
                ScheduleNextAnimationFrame();
                return 0;
            }
            if (wparam == kRenderTimerId) {
                KillTimer(hwnd_, kRenderTimerId);
                render_timer_armed_ = false;
                last_interactive_render_tick_ = GetTickCount64();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (wparam == kStartupScanTimerId) {
                KillTimer(hwnd_, kStartupScanTimerId);
                if (!deferred_directory_build_pending_ || current_image_.type == ImageType::None) {
                    return 0;
                }
                deferred_directory_build_pending_ = false;

                const std::wstring current_norm = NormalizePathLower(current_image_.path);
                if (current_norm != deferred_directory_target_norm_) {
                    return 0;
                }

                BuildDirectoryList(current_image_.path);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case kMenuOpenFile:
                    OpenFileDialog();
                    return 0;
                case kMenuOpenContainingFolder:
                    if (!OpenCurrentImageInFolder()) {
                        MessageBoxW(hwnd_,
                                    L"\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939\u5931\u8d25\uff0c\u6587\u4ef6\u53ef\u80fd\u4e0d\u5b58\u5728\u6216\u8def\u5f84\u4e0d\u53ef\u8bbf\u95ee\u3002",
                                    L"Qmi",
                                    MB_ICONERROR | MB_OK);
                    }
                    return 0;
                case kMenuPrintImage:
                    if (!PrintCurrentImage()) {
                        MessageBoxW(hwnd_,
                                    L"\u6253\u5370\u56fe\u7247\u5931\u8d25\uff0c\u8bf7\u786e\u8ba4\u7cfb\u7edf\u5df2\u914d\u7f6e\u6253\u5370\u673a\uff0c\u5e76\u5177\u6709\u53ef\u7528\u7684\u9ed8\u8ba4\u6253\u5370\u7a0b\u5e8f\u3002",
                                    L"Qmi",
                                    MB_ICONERROR | MB_OK);
                    }
                    return 0;
                case kMenuCopyImage:
                    CopyCurrentImageToClipboard();
                    return 0;
                case kMenuCopyFile:
                    CopyCurrentFileToClipboard();
                    return 0;
                case kMenuCopyImagePath:
                    CopyCurrentImagePathToClipboard();
                    return 0;
                case kMenuDeleteFile:
                    if (!MoveCurrentFileToRecycleBin()) {
                        MessageBoxW(hwnd_,
                                    L"\u5220\u9664\u6587\u4ef6\u5931\u8d25\uff0c\u6587\u4ef6\u53ef\u80fd\u4e0d\u5b58\u5728\u6216\u65e0\u6cd5\u79fb\u5165\u56de\u6536\u7ad9\u3002",
                                    L"Qmi",
                                    MB_ICONERROR | MB_OK);
                    }
                    return 0;
                case kMenuRotateClockwise:
                    RotateImageClockwise();
                    return 0;
                case kMenuRotateCounterclockwise:
                    RotateImageCounterclockwise();
                    return 0;
                case kMenuFlipHorizontal:
                    ToggleImageFlipHorizontal();
                    return 0;
                case kMenuFlipVertical:
                    ToggleImageFlipVertical();
                    return 0;
                case kMenuToggleTopMost:
                    if (!ToggleWindowTopMost()) {
                        MessageBoxW(hwnd_, L"\u8bbe\u7f6e\u7a97\u53e3\u7f6e\u9876\u5931\u8d25\u3002", L"Qmi", MB_ICONERROR | MB_OK);
                    }
                    return 0;
                case kMenuSettings:
                    OpenSettingsWindow();
                    return 0;
                case kMenuExit:
                    DestroyWindow(hwnd_);
                    return 0;
                default:
                    break;
            }
            break;

        case WM_DESTROY:
            ClearAnimationState();
            if (render_timer_armed_) {
                KillTimer(hwnd_, kRenderTimerId);
                render_timer_armed_ = false;
            }
            if (settings_hwnd_ && IsWindow(settings_hwnd_)) {
                DestroyWindow(settings_hwnd_);
                settings_hwnd_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

LRESULT CALLBACK QmiApp::MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* app = reinterpret_cast<QmiApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    }

    auto* app = reinterpret_cast<QmiApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!app) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return app->HandleMessage(msg, wparam, lparam);
}

LRESULT CALLBACK QmiApp::SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* state = new SettingsWindowState();
        state->app = reinterpret_cast<QmiApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    auto* state = reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto* app = state ? state->app : nullptr;
    switch (msg) {
        case WM_CREATE: {
            if (!state) {
                return -1;
            }

            state->nav_font = CreateFontW(-18, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                          L"Segoe UI");
            state->body_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                           L"Segoe UI");
            state->about_title_font = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                                  DEFAULT_PITCH, L"Segoe UI");
            state->about_link_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET,
                                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                                 DEFAULT_PITCH, L"Segoe UI");
            state->about_icon_border_brush = CreateSolidBrush(RGB(198, 203, 211));

            state->nav_list = CreateWindowExW(0,
                                              L"LISTBOX",
                                              nullptr,
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS |
                                                  LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                                              0,
                                              0,
                                              0,
                                              0,
                                              hwnd,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlSettingsNav)),
                                              nullptr,
                                              nullptr);

            state->nav_divider = CreateWindowExW(0,
                                                 L"STATIC",
                                                 nullptr,
                                                 WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 hwnd,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr);
            if (state->nav_list) {
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5e38\u89c4"));
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5173\u8054"));
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5feb\u6377\u952e"));
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5173\u4e8e"));
                SendMessageW(state->nav_list, LB_SETCURSEL, static_cast<WPARAM>(SettingsPage::General), 0);
            }

            state->fit_checkbox = CreateWindowExW(0,
                                                  L"BUTTON",
                                                  L"\u5207\u6362\u56fe\u7247\u65f6\u81ea\u52a8\u9002\u914d\u7a97\u53e3",
                                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_FLAT,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  hwnd,
                                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlFitOnSwitch)),
                                                  nullptr,
                                                  nullptr);
            state->smooth_checkbox = CreateWindowExW(0,
                                                     L"BUTTON",
                                                     L"\u7f29\u653e\u65f6\u4f7f\u7528\u5e73\u6ed1\u63d2\u503c",
                                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_FLAT,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     hwnd,
                                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlSmoothSampling)),
                                                     nullptr,
                                                     nullptr);
            state->opacity_label = CreateWindowExW(0,
                                                   L"STATIC",
                                                   L"\u80cc\u666f\u900f\u660e\u5ea6",
                                                   WS_CHILD | WS_VISIBLE,
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   hwnd,
                                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlWindowOpacityLabel)),
                                                   nullptr,
                                                   nullptr);
            state->opacity_slider = CreateWindowExW(0,
                                                    TRACKBAR_CLASSW,
                                                    nullptr,
                                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS |
                                                        TBS_TRANSPARENTBKGND,
                                                    0,
                                                    0,
                                                    0,
                                                    0,
                                                    hwnd,
                                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlWindowOpacitySlider)),
                                                    nullptr,
                                                    nullptr);
            state->opacity_value_label = CreateWindowExW(0,
                                                         L"STATIC",
                                                         L"100%",
                                                         WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                                         0,
                                                         0,
                                                         0,
                                                         0,
                                                         hwnd,
                                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlWindowOpacityValue)),
                                                         nullptr,
                                                         nullptr);
            state->sort_field_label = CreateWindowExW(
                0,
                L"STATIC",
                L"\u80f6\u7247\u6392\u5e8f\u65b9\u5f0f",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlFilmStripSortFieldLabel)),
                nullptr,
                nullptr);
            state->sort_field_combo = CreateWindowExW(
                0,
                WC_COMBOBOXW,
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlFilmStripSortFieldCombo)),
                nullptr,
                nullptr);
            state->sort_direction_label = CreateWindowExW(
                0,
                L"STATIC",
                L"\u6392\u5e8f\u65b9\u5411",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlFilmStripSortDirectionLabel)),
                nullptr,
                nullptr);
            state->sort_direction_combo = CreateWindowExW(
                0,
                WC_COMBOBOXW,
                nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlFilmStripSortDirectionCombo)),
                nullptr,
                nullptr);
            if (state->sort_field_combo) {
                SendMessageW(state->sort_field_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6309\u6587\u4ef6\u540d"));
                SendMessageW(state->sort_field_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6309\u6587\u4ef6\u5927\u5c0f"));
                SendMessageW(state->sort_field_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6309\u6587\u4ef6\u540e\u7f00"));
                SendMessageW(state->sort_field_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6309\u521b\u5efa\u65e5\u671f"));
                SendMessageW(state->sort_field_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u6309\u4fee\u6539\u65e5\u671f"));
            }
            if (state->sort_direction_combo) {
                SendMessageW(state->sort_direction_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5347\u5e8f"));
                SendMessageW(state->sort_direction_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u964d\u5e8f"));
            }
            SendMessageW(state->fit_checkbox, BM_SETCHECK, app && app->fit_on_switch_ ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(state->smooth_checkbox,
                         BM_SETCHECK,
                         app && app->smooth_sampling_ ? BST_CHECKED : BST_UNCHECKED,
                         0);
            if (state->opacity_slider) {
                SendMessageW(state->opacity_slider, TBM_SETRANGEMIN, FALSE, kMinWindowOpacityPercent);
                SendMessageW(state->opacity_slider, TBM_SETRANGEMAX, TRUE, kMaxWindowOpacityPercent);
                SendMessageW(state->opacity_slider, TBM_SETTICFREQ, 10, 0);
                SendMessageW(state->opacity_slider, TBM_SETLINESIZE, 0, 1);
                SendMessageW(state->opacity_slider, TBM_SETPAGESIZE, 0, 5);
                SendMessageW(state->opacity_slider,
                             TBM_SETPOS,
                             TRUE,
                             app ? ClampWindowOpacityPercent(app->window_opacity_percent_) : kMaxWindowOpacityPercent);
            }
            UpdateWindowOpacityLabel(state, app ? app->window_opacity_percent_ : kMaxWindowOpacityPercent);
            SyncFilmStripSortControls(state,
                                      app ? app->film_strip_sort_field_ : kFilmStripSortFieldFileName,
                                      app ? app->film_strip_sort_descending_ : false);

            state->associations_hint = CreateWindowExW(0,
                                                       L"STATIC",
                                                       L"\u9009\u62e9\u9700\u8981\u7531 Qmi \u6253\u5f00\u7684\u6587\u4ef6\u7c7b\u578b\uff0c\u7136\u540e\u70b9\u51fb\u201c\u5e94\u7528\u5173\u8054\u201d\u3002",
                                                       WS_CHILD | WS_VISIBLE,
                                                       0,
                                                       0,
                                                       0,
                                                       0,
                                                       hwnd,
                                                       nullptr,
                                                       nullptr,
                                                       nullptr);

            state->association_checkboxes.reserve(kAssociationTypes.size());
            for (size_t i = 0; i < kAssociationTypes.size(); ++i) {
                HWND checkbox = CreateWindowExW(
                    0,
                    L"BUTTON",
                    kAssociationTypes[i].label,
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_FLAT,
                    0,
                    0,
                    0,
                    0,
                    hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(AssociationCheckboxControlId(i))),
                    nullptr,
                    nullptr);
                state->association_checkboxes.push_back(checkbox);
            }

            state->association_select_all_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u5168\u9009",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlAssociationSelectAll)),
                nullptr,
                nullptr);

            state->association_clear_all_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u5168\u4e0d\u9009",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlAssociationClearAll)),
                nullptr,
                nullptr);

            state->association_apply_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u5e94\u7528\u5173\u8054",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlAssociationApply)),
                nullptr,
                nullptr);

            state->association_status = CreateWindowExW(0,
                                                        L"STATIC",
                                                        L"\u52fe\u9009\u6269\u5c55\u540d\u540e\uff0c\u70b9\u51fb\u201c\u5e94\u7528\u5173\u8054\u201d\u4fdd\u5b58\u66f4\u6539\u3002",
                                                        WS_CHILD | WS_VISIBLE,
                                                        0,
                                                        0,
                                                        0,
                                                        0,
                                                        hwnd,
                                                        nullptr,
                                                        nullptr,
                                                        nullptr);
            SyncAssociationSelections(state);

            state->about_icon_border =
                CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            state->about_icon = CreateWindowExW(
                0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            state->about_title =
                CreateWindowExW(0, L"STATIC", L"Qmi", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
            state->about_description = CreateWindowExW(
                0,
                L"STATIC",
                L"\u8f7b\u91cf\u7ea7 Windows \u770b\u56fe\u5de5\u5177\uff0c\u652f\u6301 jpg / jpeg / png / bmp / ico / webp / gif / heic / heif / svg",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                nullptr,
                nullptr);
            state->about_author = CreateWindowExW(0,
                                                  L"STATIC",
                                                  (std::wstring(L"\u4f5c\u8005\uff1a") + kQmiAuthorName).c_str(),
                                                  WS_CHILD | WS_VISIBLE,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  hwnd,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr);
            state->about_repo_label = CreateWindowExW(0,
                                                      L"STATIC",
                                                      L"\u4ed3\u5e93\uff1a",
                                                      WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                                                      0,
                                                      0,
                                                      0,
                                                      0,
                                                      hwnd,
                                                      nullptr,
                                                      nullptr,
                                                      nullptr);
            state->about_repo_link = CreateWindowExW(0,
                                                     L"STATIC",
                                                     kQmiRepositoryUrl,
                                                     WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     hwnd,
                                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlAboutRepoLink)),
                                                     nullptr,
                                                     nullptr);
            if (state->about_icon) {
                HICON about_icon = LoadAppIcon(app ? app->hinstance_ : nullptr, 64, 64);
                if (!about_icon) {
                    about_icon = LoadIconW(nullptr, IDI_APPLICATION);
                }
                if (about_icon) {
                    SendMessageW(state->about_icon, STM_SETICON, reinterpret_cast<WPARAM>(about_icon), 0);
                }
            }
            state->shortcuts_table = CreateWindowExW(0,
                                                     WC_LISTVIEWW,
                                                     nullptr,
                                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT |
                                                         LVS_SINGLESEL | LVS_NOSORTHEADER,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     hwnd,
                                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCtrlShortcutsTable)),
                                                     nullptr,
                                                     nullptr);
            InitializeShortcutsTable(state->shortcuts_table);

            SetControlFont(state->nav_list, state->nav_font);
            SetControlFont(state->fit_checkbox, state->body_font);
            SetControlFont(state->smooth_checkbox, state->body_font);
            SetControlFont(state->opacity_label, state->body_font);
            SetControlFont(state->opacity_value_label, state->body_font);
            SetControlFont(state->sort_field_label, state->body_font);
            SetControlFont(state->sort_field_combo, state->body_font);
            SetControlFont(state->sort_direction_label, state->body_font);
            SetControlFont(state->sort_direction_combo, state->body_font);
            SetControlFont(state->associations_hint, state->body_font);
            for (HWND checkbox : state->association_checkboxes) {
                SetControlFont(checkbox, state->body_font);
            }
            SetControlFont(state->association_select_all_button, state->body_font);
            SetControlFont(state->association_clear_all_button, state->body_font);
            SetControlFont(state->association_apply_button, state->body_font);
            SetControlFont(state->association_status, state->body_font);
            SetControlFont(state->shortcuts_table, state->body_font);
            SetControlFont(state->about_icon, state->body_font);
            SetControlFont(state->about_title, state->about_title_font ? state->about_title_font : state->body_font);
            SetControlFont(state->about_description, state->body_font);
            SetControlFont(state->about_author, state->body_font);
            SetControlFont(state->about_repo_label, state->body_font);
            SetControlFont(state->about_repo_link, state->about_link_font ? state->about_link_font : state->body_font);

            SetActiveSettingsPage(state, static_cast<int>(SettingsPage::General));
            LayoutSettingsWindow(hwnd, state);
            return 0;
        }
        case WM_GETMINMAXINFO:
            if (auto* minmax = reinterpret_cast<MINMAXINFO*>(lparam)) {
                minmax->ptMinTrackSize.x = 520;
                minmax->ptMinTrackSize.y = 320;
            }
            return 0;
        case WM_SIZE:
            LayoutSettingsWindow(hwnd, state);
            return 0;
        case WM_MEASUREITEM:
            if (state) {
                auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (measure && measure->CtlID == kCtrlSettingsNav) {
                    measure->itemHeight = 40;
                    return TRUE;
                }
            }
            break;
        case WM_DRAWITEM:
            if (state) {
                auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if (draw && draw->CtlID == kCtrlSettingsNav && draw->CtlType == ODT_LISTBOX) {
                    FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_WINDOW));
                    if (draw->itemID == static_cast<UINT>(-1)) {
                        return TRUE;
                    }

                    RECT item_rect = draw->rcItem;
                    InflateRect(&item_rect, -6, -4);
                    const bool active = static_cast<int>(draw->itemID) == state->active_page;
                    if (active) {
                        HPEN accent_pen = CreatePen(PS_SOLID, 2, RGB(37, 74, 160));
                        HPEN old_pen = static_cast<HPEN>(SelectObject(draw->hDC, accent_pen));
                        MoveToEx(draw->hDC, item_rect.left + 4, item_rect.top + 6, nullptr);
                        LineTo(draw->hDC, item_rect.left + 4, item_rect.bottom - 6);
                        SelectObject(draw->hDC, old_pen);
                        DeleteObject(accent_pen);
                    }

                    wchar_t text[64] = {};
                    SendMessageW(state->nav_list, LB_GETTEXT, draw->itemID, reinterpret_cast<LPARAM>(text));
                    SetBkMode(draw->hDC, TRANSPARENT);
                    SetTextColor(draw->hDC, active ? RGB(37, 74, 160) : RGB(75, 82, 97));
                    DrawTextW(draw->hDC, text, -1, &item_rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                    return TRUE;
                }
            }
            break;
        case WM_COMMAND:
            if (!state) {
                return 0;
            }
            if (LOWORD(wparam) == kCtrlSettingsNav && HIWORD(wparam) == LBN_SELCHANGE) {
                const LRESULT selection = SendMessageW(state->nav_list, LB_GETCURSEL, 0, 0);
                if (selection != LB_ERR) {
                    SetActiveSettingsPage(state, static_cast<int>(selection));
                }
                return 0;
            }
            if (app && HIWORD(wparam) == CBN_SELCHANGE && IsFilmStripSortControlId(LOWORD(wparam))) {
                int new_sort_field = app->film_strip_sort_field_;
                bool new_sort_descending = app->film_strip_sort_descending_;
                if (state->sort_field_combo) {
                    const LRESULT selected = SendMessageW(state->sort_field_combo, CB_GETCURSEL, 0, 0);
                    if (selected != CB_ERR) {
                        new_sort_field = ClampFilmStripSortField(static_cast<int>(selected));
                    }
                }
                if (state->sort_direction_combo) {
                    const LRESULT selected = SendMessageW(state->sort_direction_combo, CB_GETCURSEL, 0, 0);
                    if (selected != CB_ERR) {
                        new_sort_descending = ClampFilmStripSortDirectionIndex(static_cast<int>(selected)) != 0;
                    }
                }

                const bool changed = new_sort_field != app->film_strip_sort_field_ ||
                                     new_sort_descending != app->film_strip_sort_descending_;
                if (changed) {
                    app->film_strip_sort_field_ = new_sort_field;
                    app->film_strip_sort_descending_ = new_sort_descending;
                    SaveUserConfig(app->fit_on_switch_,
                                   app->smooth_sampling_,
                                   app->window_opacity_percent_,
                                   app->film_strip_sort_field_,
                                   app->film_strip_sort_descending_);
                    if (!app->current_image_.path.empty()) {
                        app->BuildDirectoryList(app->current_image_.path);
                    }
                    app->RequestRender();
                }
                SyncFilmStripSortControls(state, app->film_strip_sort_field_, app->film_strip_sort_descending_);
                return 0;
            }
            if (HIWORD(wparam) == STN_CLICKED && LOWORD(wparam) == kCtrlAboutRepoLink) {
                const HINSTANCE shell_result =
                    ShellExecuteW(hwnd, L"open", kQmiRepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(shell_result) <= 32) {
                    MessageBoxW(hwnd,
                                L"\u65e0\u6cd5\u6253\u5f00\u4ed3\u5e93\u5730\u5740\u3002",
                                L"Qmi",
                                MB_ICONWARNING | MB_OK);
                }
                return 0;
            }
            if (HIWORD(wparam) == BN_CLICKED) {
                const int control_id = LOWORD(wparam);
                if (control_id == kCtrlAssociationSelectAll) {
                    SetAllAssociationSelections(state, true);
                    SetAssociationStatus(state,
                                         L"\u5df2\u5168\u9009\uff0c\u8bf7\u70b9\u51fb\u201c\u5e94\u7528\u5173\u8054\u201d\u4fdd\u5b58\u66f4\u6539\u3002");
                    return 0;
                }
                if (control_id == kCtrlAssociationClearAll) {
                    SetAllAssociationSelections(state, false);
                    SetAssociationStatus(state,
                                         L"\u5df2\u5168\u4e0d\u9009\uff0c\u8bf7\u70b9\u51fb\u201c\u5e94\u7528\u5173\u8054\u201d\u4fdd\u5b58\u66f4\u6539\u3002");
                    return 0;
                }
                if (control_id == kCtrlAssociationApply) {
                    std::wstring result;
                    ApplyAssociationSelectionFromUi(state, &result);
                    SetAssociationStatus(state, result);
                    SyncAssociationSelections(state);
                    return 0;
                }
                if (IsAssociationCheckboxControlId(control_id)) {
                    SetAssociationStatus(state, L"\u66f4\u6539\u5c1a\u672a\u5e94\u7528\uff0c\u8bf7\u70b9\u51fb\u201c\u5e94\u7528\u5173\u8054\u201d\u3002");
                    return 0;
                }
            }
            if (app && HIWORD(wparam) == BN_CLICKED) {
                bool should_save_user_config = false;
                if (LOWORD(wparam) == kCtrlFitOnSwitch && lparam) {
                    app->fit_on_switch_ = SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    should_save_user_config = true;
                } else if (LOWORD(wparam) == kCtrlSmoothSampling && lparam) {
                    app->smooth_sampling_ =
                        SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    should_save_user_config = true;
                }
                if (should_save_user_config) {
                    SaveUserConfig(app->fit_on_switch_,
                                   app->smooth_sampling_,
                                   app->window_opacity_percent_,
                                   app->film_strip_sort_field_,
                                   app->film_strip_sort_descending_);
                }
                InvalidateRect(app->hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_HSCROLL:
            if (state && app && state->opacity_slider && reinterpret_cast<HWND>(lparam) == state->opacity_slider) {
                const LRESULT slider_pos = SendMessageW(state->opacity_slider, TBM_GETPOS, 0, 0);
                const int new_opacity_percent = ClampWindowOpacityPercent(static_cast<int>(slider_pos));
                UpdateWindowOpacityLabel(state, new_opacity_percent);
                if (new_opacity_percent != app->window_opacity_percent_) {
                    app->window_opacity_percent_ = new_opacity_percent;
                    app->UpdateBackgroundOpacityBrushes();
                    SaveUserConfig(app->fit_on_switch_,
                                   app->smooth_sampling_,
                                   app->window_opacity_percent_,
                                   app->film_strip_sort_field_,
                                   app->film_strip_sort_descending_);
                    app->RequestRender();
                }
                return 0;
            }
            break;
        case WM_SETCURSOR:
            if (state && state->about_repo_link && reinterpret_cast<HWND>(wparam) == state->about_repo_link) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_CTLCOLORLISTBOX:
            if (state && reinterpret_cast<HWND>(lparam) == state->nav_list) {
                SetTextColor(reinterpret_cast<HDC>(wparam), RGB(43, 48, 59));
                SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (state) {
                const HWND ctrl = reinterpret_cast<HWND>(lparam);
                HDC hdc = reinterpret_cast<HDC>(wparam);
                if (ctrl == state->opacity_slider) {
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
                }
                if (ctrl == state->association_select_all_button || ctrl == state->association_clear_all_button ||
                    ctrl == state->association_apply_button) {
                    break;
                }
                bool is_association_checkbox = false;
                for (HWND checkbox : state->association_checkboxes) {
                    if (ctrl == checkbox) {
                        is_association_checkbox = true;
                        break;
                    }
                }

                if (ctrl == state->about_repo_link) {
                    SetTextColor(hdc, RGB(36, 92, 196));
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
                }
                if (ctrl == state->about_icon_border) {
                    SetBkMode(hdc, OPAQUE);
                    return reinterpret_cast<INT_PTR>(state->about_icon_border_brush ? state->about_icon_border_brush
                                                                                     : GetSysColorBrush(COLOR_3DLIGHT));
                }
                if (ctrl == state->about_icon) {
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
                }
                if (ctrl == state->about_title) {
                    SetTextColor(hdc, RGB(25, 33, 52));
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
                }

                const bool is_panel_ctrl = ctrl == state->fit_checkbox || ctrl == state->smooth_checkbox ||
                                           ctrl == state->opacity_label || ctrl == state->opacity_value_label ||
                                           ctrl == state->sort_field_label || ctrl == state->sort_direction_label ||
                                           ctrl == state->associations_hint || ctrl == state->association_status ||
                                           ctrl == state->about_description || ctrl == state->about_author ||
                                           ctrl == state->about_repo_label || is_association_checkbox;
                if (is_panel_ctrl) {
                    SetTextColor(hdc, RGB(52, 58, 70));
                    SetBkMode(hdc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
                }

                SetTextColor(hdc, RGB(36, 42, 52));
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state && state->app) {
                app->settings_hwnd_ = nullptr;
            }
            return 0;
        case WM_NCDESTROY:
            if (state) {
                if (state->nav_font) {
                    DeleteObject(state->nav_font);
                }
                if (state->body_font) {
                    DeleteObject(state->body_font);
                }
                if (state->about_title_font) {
                    DeleteObject(state->about_title_font);
                }
                if (state->about_link_font) {
                    DeleteObject(state->about_link_font);
                }
                if (state->about_icon_border_brush) {
                    DeleteObject(state->about_icon_border_brush);
                }
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, PWSTR, int show_cmd) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::optional<std::wstring> startup_path;
    if (argv && argc > 1) {
        startup_path = argv[1];
    }
    if (argv) {
        LocalFree(argv);
    }

    QmiApp app;
    if (!app.Initialize(hinstance, show_cmd, startup_path)) {
        MessageBoxW(nullptr, L"Qmi \u521d\u59cb\u5316\u5931\u8d25\u3002", L"Qmi", MB_ICONERROR | MB_OK);
        return 1;
    }
    return app.Run();
}
