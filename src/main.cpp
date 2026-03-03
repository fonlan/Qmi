#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

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

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

class QmiApp;

namespace {
constexpr wchar_t kMainClassName[] = L"QmiMainWindowClass";
constexpr wchar_t kSettingsClassName[] = L"QmiSettingsWindowClass";

constexpr UINT kMenuOpenFile = 1001;
constexpr UINT kMenuSettings = 1002;
constexpr UINT kMenuExit = 1003;
constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT_PTR kStartupScanTimerId = 2;
constexpr UINT_PTR kAnimationTimerId = 3;

constexpr int kCtrlFitOnSwitch = 2001;
constexpr int kCtrlSmoothSampling = 2002;
constexpr int kCtrlSettingsNav = 2100;

constexpr int kMinWindowWidth = 640;
constexpr int kMinWindowHeight = 420;
constexpr int kResizeBorder = 8;
constexpr int kTitleButtonWidth = 46;
constexpr int kTitleButtonHeight = 34;
constexpr float kViewportMargin = 0.0f;
constexpr float kViewportBottomGap = 0.0f;
constexpr ULONGLONG kInteractiveFrameIntervalMs = 16;
constexpr UINT kGifDefaultDelayMs = 100;
constexpr UINT kGifMinDelayMs = 16;
constexpr int kThumbnailDecodeBudgetPerFrame = 1;
constexpr BYTE kUiChromeAlpha = 200;
constexpr float kUiChromeOpacity = static_cast<float>(kUiChromeAlpha) / 255.0f;
constexpr float kViewportLetterboxOpacity = 0.50f;
constexpr float kThumbnailCellOpacity = 0.58f;

template <typename T>
T Clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return s;
}

std::wstring NormalizePathLower(const fs::path& p) {
    std::error_code ec;
    fs::path absolute = fs::absolute(p, ec);
    if (ec) {
        absolute = p;
    }
    return ToLower(absolute.lexically_normal().wstring());
}

bool IsSupportedExtension(const fs::path& p) {
    const std::wstring ext = ToLower(p.extension().wstring());
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".bmp" || ext == L".webp" ||
           ext == L".gif" || ext == L".svg";
}

bool IsWebpExtension(const fs::path& p) {
    return ToLower(p.extension().wstring()) == L".webp";
}

bool IsGifExtension(const fs::path& p) {
    return ToLower(p.extension().wstring()) == L".gif";
}

bool TryReadMetadataUInt(IWICMetadataQueryReader* reader, const wchar_t* query, UINT* out_value) {
    if (!reader || !query || !out_value) {
        return false;
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT hr = reader->GetMetadataByName(query, &value);
    if (FAILED(hr)) {
        PropVariantClear(&value);
        return false;
    }

    bool ok = true;
    switch (value.vt) {
        case VT_UI1:
            *out_value = value.bVal;
            break;
        case VT_UI2:
            *out_value = value.uiVal;
            break;
        case VT_UI4:
            *out_value = value.ulVal;
            break;
        case VT_I1:
            *out_value = value.cVal < 0 ? 0u : static_cast<UINT>(value.cVal);
            break;
        case VT_I2:
            *out_value = value.iVal < 0 ? 0u : static_cast<UINT>(value.iVal);
            break;
        case VT_I4:
            *out_value = value.lVal < 0 ? 0u : static_cast<UINT>(value.lVal);
            break;
        default:
            ok = false;
            break;
    }

    PropVariantClear(&value);
    return ok;
}

void ClearCanvasRect(std::vector<std::uint8_t>& canvas,
                     UINT canvas_width,
                     UINT canvas_height,
                     UINT left,
                     UINT top,
                     UINT width,
                     UINT height) {
    if (canvas.empty() || canvas_width == 0 || canvas_height == 0 || width == 0 || height == 0) {
        return;
    }

    const UINT x0 = std::min(left, canvas_width);
    const UINT y0 = std::min(top, canvas_height);
    const UINT x1 = std::min(canvas_width, x0 + width);
    const UINT y1 = std::min(canvas_height, y0 + height);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    const size_t stride = static_cast<size_t>(canvas_width) * 4u;
    const size_t clear_width_bytes = static_cast<size_t>(x1 - x0) * 4u;
    for (UINT y = y0; y < y1; ++y) {
        auto* row = canvas.data() + static_cast<size_t>(y) * stride + static_cast<size_t>(x0) * 4u;
        memset(row, 0, clear_width_bytes);
    }
}

inline void BlendPremultipliedPixel(std::uint8_t* dst, const std::uint8_t* src) {
    const UINT src_a = src[3];
    if (src_a == 0) {
        return;
    }
    if (src_a == 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        return;
    }

    const UINT inv_src_a = 255u - src_a;
    dst[0] = static_cast<std::uint8_t>(src[0] + ((static_cast<UINT>(dst[0]) * inv_src_a + 127u) / 255u));
    dst[1] = static_cast<std::uint8_t>(src[1] + ((static_cast<UINT>(dst[1]) * inv_src_a + 127u) / 255u));
    dst[2] = static_cast<std::uint8_t>(src[2] + ((static_cast<UINT>(dst[2]) * inv_src_a + 127u) / 255u));
    dst[3] = static_cast<std::uint8_t>(src_a + ((static_cast<UINT>(dst[3]) * inv_src_a + 127u) / 255u));
}

void CompositeFrame(std::vector<std::uint8_t>& canvas,
                    UINT canvas_width,
                    UINT canvas_height,
                    const std::vector<std::uint8_t>& frame,
                    UINT frame_width,
                    UINT frame_height,
                    UINT offset_x,
                    UINT offset_y) {
    if (canvas.empty() || frame.empty() || canvas_width == 0 || canvas_height == 0 || frame_width == 0 || frame_height == 0) {
        return;
    }

    const UINT x0 = std::min(offset_x, canvas_width);
    const UINT y0 = std::min(offset_y, canvas_height);
    const UINT draw_w = std::min(frame_width, canvas_width - x0);
    const UINT draw_h = std::min(frame_height, canvas_height - y0);
    if (draw_w == 0 || draw_h == 0) {
        return;
    }

    const size_t canvas_stride = static_cast<size_t>(canvas_width) * 4u;
    const size_t frame_stride = static_cast<size_t>(frame_width) * 4u;

    for (UINT y = 0; y < draw_h; ++y) {
        std::uint8_t* dst_row =
            canvas.data() + static_cast<size_t>(y0 + y) * canvas_stride + static_cast<size_t>(x0) * 4u;
        const std::uint8_t* src_row = frame.data() + static_cast<size_t>(y) * frame_stride;
        for (UINT x = 0; x < draw_w; ++x) {
            BlendPremultipliedPixel(dst_row + static_cast<size_t>(x) * 4u, src_row + static_cast<size_t>(x) * 4u);
        }
    }
}

std::optional<fs::path> FindFirstSupportedImageInDirectory(const fs::path& directory) {
    std::error_code iter_ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, iter_ec);
    fs::directory_iterator end;
    if (iter_ec) {
        return std::nullopt;
    }

    for (; it != end; it.increment(iter_ec)) {
        if (iter_ec) {
            return std::nullopt;
        }
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || file_ec) {
            continue;
        }
        const fs::path entry_path = it->path();
        if (IsSupportedExtension(entry_path)) {
            return entry_path;
        }
    }
    return std::nullopt;
}

enum class ImageType {
    None,
    Raster,
    Svg
};

enum class TitleButton {
    None,
    Minimize,
    Maximize,
    Close
};

struct TitleButtons {
    RECT min_rect{};
    RECT max_rect{};
    RECT close_rect{};
};

struct Thumbnail {
    bool attempted = false;
    bool failed = false;
    ComPtr<ID2D1Bitmap1> bitmap;
    float width = 0.0f;
    float height = 0.0f;
};

struct VisibleThumb {
    int index = -1;
    D2D1_RECT_F rect{};
};

struct LoadedImage {
    ImageType type = ImageType::None;
    fs::path path;
    ComPtr<ID2D1Bitmap1> raster;
    ComPtr<ID2D1SvgDocument> svg;
    float width = 0.0f;
    float height = 0.0f;
};

enum class SettingsPage {
    General = 0,
    Associations = 1,
    About = 2
};

struct SettingsWindowState {
    QmiApp* app = nullptr;
    int active_page = static_cast<int>(SettingsPage::General);

    HWND nav_list = nullptr;

    HWND general_title = nullptr;
    HWND fit_checkbox = nullptr;
    HWND smooth_checkbox = nullptr;

    HWND associations_title = nullptr;
    HWND associations_text = nullptr;

    HWND about_title = nullptr;
    HWND about_text = nullptr;

    HFONT title_font = nullptr;
    HFONT nav_font = nullptr;
    HFONT body_font = nullptr;
};

void SetControlFont(HWND hwnd, HFONT font) {
    if (!hwnd || !font) {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void SetActiveSettingsPage(SettingsWindowState* state, int page_index) {
    if (!state) {
        return;
    }
    state->active_page =
        Clamp(page_index, static_cast<int>(SettingsPage::General), static_cast<int>(SettingsPage::About));
    const bool show_general = state->active_page == static_cast<int>(SettingsPage::General);
    const bool show_associations = state->active_page == static_cast<int>(SettingsPage::Associations);
    const bool show_about = state->active_page == static_cast<int>(SettingsPage::About);

    ShowWindow(state->general_title, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->fit_checkbox, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->smooth_checkbox, show_general ? SW_SHOW : SW_HIDE);

    ShowWindow(state->associations_title, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->associations_text, show_associations ? SW_SHOW : SW_HIDE);

    ShowWindow(state->about_title, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_text, show_about ? SW_SHOW : SW_HIDE);
    if (state->nav_list) {
        InvalidateRect(state->nav_list, nullptr, FALSE);
    }
}

void LayoutSettingsWindow(HWND hwnd, SettingsWindowState* state) {
    if (!hwnd || !state) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int client_width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int client_height = std::max(1, static_cast<int>(rc.bottom - rc.top));

    constexpr int kOuterPadding = 20;
    constexpr int kNavWidth = 130;
    constexpr int kColumnsGap = 22;
    constexpr int kPanelPaddingX = 16;

    const int nav_height = std::max(120, client_height - (kOuterPadding * 2));
    MoveWindow(state->nav_list, kOuterPadding, kOuterPadding, kNavWidth, nav_height, TRUE);

    const int panel_x = kOuterPadding + kNavWidth + kColumnsGap;
    const int panel_width = std::max(160, client_width - panel_x - kOuterPadding);
    const int panel_y = kOuterPadding;
    const int panel_height = std::max(120, client_height - (kOuterPadding * 2));

    const int text_width = std::max(80, panel_width - kPanelPaddingX * 2);
    MoveWindow(state->general_title, panel_x + kPanelPaddingX, panel_y + 8, text_width, 34, TRUE);
    MoveWindow(state->fit_checkbox, panel_x + kPanelPaddingX, panel_y + 56, text_width, 28, TRUE);
    MoveWindow(state->smooth_checkbox, panel_x + kPanelPaddingX, panel_y + 92, text_width, 28, TRUE);

    MoveWindow(state->associations_title, panel_x + kPanelPaddingX, panel_y + 8, text_width, 34, TRUE);
    MoveWindow(state->associations_text, panel_x + kPanelPaddingX, panel_y + 56, text_width, std::max(40, panel_height - 72),
               TRUE);

    MoveWindow(state->about_title, panel_x + kPanelPaddingX, panel_y + 8, text_width, 34, TRUE);
    MoveWindow(state->about_text, panel_x + kPanelPaddingX, panel_y + 56, text_width, std::max(40, panel_height - 72), TRUE);
}
}  // namespace

class QmiApp {
public:
    QmiApp() = default;
    ~QmiApp() = default;

    bool Initialize(HINSTANCE hinstance, int show_cmd, const std::optional<std::wstring>& startup_path);
    int Run();

private:
    bool RegisterWindowClasses();
    bool CreateMainWindow();
    bool InitDeviceIndependentResources();
    bool InitDeviceResources();
    bool CreateWindowSizeResources();
    bool PresentLayeredFrame();
    void DiscardDeviceResources();
    void ReleaseLayeredBitmap();
    void CreateBrushes();
    void ApplyWindowBackdrop();
    void ResetView();

    bool OpenImagePath(const fs::path& path, bool reset_view = true, bool defer_directory_scan = false);
    bool LoadImageByIndex(int index, bool reset_view = true);
    void BuildDirectoryList(const fs::path& selected_file);
    void ScheduleDeferredDirectoryBuild(const fs::path& selected_file);
    void TryOpenInitialImage(const std::optional<std::wstring>& startup_path);
    void MoveSelection(int delta);

    HRESULT LoadRasterBitmap(const fs::path& path,
                             UINT max_width,
                             UINT max_height,
                             ID2D1Bitmap1** out_bitmap,
                             float* out_width,
                             float* out_height);
    HRESULT LoadWebpBitmap(const fs::path& path,
                           UINT max_width,
                           UINT max_height,
                           ID2D1Bitmap1** out_bitmap,
                           float* out_width,
                           float* out_height);
    HRESULT LoadGifAnimation(const fs::path& path, LoadedImage* out_image);
    HRESULT LoadSvgDocument(const fs::path& path, ID2D1SvgDocument** out_svg, float* out_width, float* out_height);
    HRESULT LoadSvgThumbnailBitmap(const fs::path& path,
                                   UINT max_width,
                                   UINT max_height,
                                   ID2D1Bitmap1** out_bitmap,
                                   float* out_width,
                                   float* out_height);
    void EnsureThumbnailLoaded(int index);
    void ClearAnimationState();
    void ScheduleNextAnimationFrame();

    void Render();
    void DrawImageRegion(const D2D1_RECT_F& viewport);
    void DrawFilmStrip(const D2D1_RECT_F& strip_rect);
    void DrawTitleButtons(const TitleButtons& buttons);
    void DrawOpenButton(const D2D1_RECT_F& viewport);
    void DrawButtonGlyph(TitleButton button, const RECT& rect);
    void DrawCenteredText(const std::wstring& text, const D2D1_RECT_F& rect, IDWriteTextFormat* format);
    void DrawMessageOverlay(const std::wstring& text);

    D2D1_RECT_F GetImageViewport(float width, float height) const;
    D2D1_RECT_F GetFilmStripRect(float width, float height) const;
    float GetBaseImageScale(const D2D1_RECT_F& viewport) const;
    D2D1_RECT_F GetImageDestinationRect(const D2D1_RECT_F& viewport) const;
    D2D1_RECT_F GetOpenButtonRect(const D2D1_RECT_F& viewport) const;
    TitleButtons GetTitleButtons(const D2D1_RECT_F& viewport) const;
    TitleButton HitTestTitleButton(POINT client_pt) const;
    bool HitTestOpenButton(POINT client_pt) const;
    bool IsPointOverVisibleImage(POINT client_pt, const D2D1_RECT_F& viewport) const;
    int HitTestThumbnail(POINT client_pt) const;

    void ShowContextMenu(POINT screen_pt);
    void OpenFileDialog();
    void OpenSettingsWindow();
    void RequestRender(bool interactive = false);

    LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HitTestNonClient(POINT screen_pt) const;
    void UpdateHoverState(POINT client_pt);
    void HandleMouseWheel(short wheel_delta, POINT screen_pt);

    static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HINSTANCE hinstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND settings_hwnd_ = nullptr;

    ComPtr<ID2D1Factory1> d2d_factory_;
    ComPtr<ID2D1Device> d2d_device_;
    ComPtr<ID2D1DeviceContext> d2d_context_;
    ComPtr<ID2D1DeviceContext5> d2d_context5_;
    ComPtr<ID2D1Bitmap1> target_bitmap_;

    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<ID3D11DeviceContext> d3d_context_;
    ComPtr<ID3D11Texture2D> frame_texture_;
    ComPtr<ID3D11Texture2D> readback_texture_;

    ComPtr<IWICImagingFactory> wic_factory_;
    ComPtr<IDWriteFactory> dwrite_factory_;
    ComPtr<IDWriteTextFormat> text_format_;
    ComPtr<IDWriteTextFormat> small_text_format_;

    ComPtr<ID2D1SolidColorBrush> brush_text_;
    ComPtr<ID2D1SolidColorBrush> brush_panel_;
    ComPtr<ID2D1SolidColorBrush> brush_overlay_;
    ComPtr<ID2D1SolidColorBrush> brush_accent_;
    ComPtr<ID2D1SolidColorBrush> brush_hover_;
    ComPtr<ID2D1SolidColorBrush> brush_close_hover_;
    ComPtr<ID2D1SolidColorBrush> brush_viewport_bg_;
    ComPtr<ID2D1SolidColorBrush> brush_image_bg_;
    ComPtr<ID2D1SolidColorBrush> brush_thumb_bg_;

    std::vector<fs::path> images_;
    std::vector<Thumbnail> thumbnails_;
    std::vector<VisibleThumb> visible_thumbs_;

    LoadedImage current_image_;
    int current_index_ = -1;
    std::wstring current_error_;

    float zoom_ = 1.0f;
    float pan_x_ = 0.0f;
    float pan_y_ = 0.0f;

    bool dragging_image_ = false;
    POINT drag_last_{};

    bool fit_on_switch_ = true;
    bool smooth_sampling_ = true;
    int film_strip_height_ = 120;
    int film_strip_scroll_index_ = -1;

    TitleButton hover_button_ = TitleButton::None;
    TitleButton pressed_button_ = TitleButton::None;
    bool hover_open_button_ = false;
    bool pressed_open_button_ = false;
    bool render_timer_armed_ = false;
    ULONGLONG last_interactive_render_tick_ = 0;
    bool deferred_directory_build_pending_ = false;
    std::wstring deferred_directory_target_norm_;
    bool bitmaps_need_reload_ = false;
    std::vector<ComPtr<ID2D1Bitmap1>> animation_frames_;
    std::vector<UINT> animation_frame_delays_ms_;
    size_t animation_frame_index_ = 0;

    HDC layered_dc_ = nullptr;
    HBITMAP layered_bitmap_ = nullptr;
    HGDIOBJ layered_prev_bitmap_ = nullptr;
    void* layered_bits_ = nullptr;
    UINT layered_width_ = 0;
    UINT layered_height_ = 0;
    UINT layered_stride_ = 0;
};

bool QmiApp::Initialize(HINSTANCE hinstance, int show_cmd, const std::optional<std::wstring>& startup_path) {
    hinstance_ = hinstance;

    const HRESULT coinit_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coinit_hr) && coinit_hr != RPC_E_CHANGED_MODE) {
        return false;
    }

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

    return true;
}

bool QmiApp::RegisterWindowClasses() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hinstance_;
    wc.lpfnWndProc = &QmiApp::MainWndProc;
    wc.lpszClassName = kMainClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    settings_wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x171717, kUiChromeOpacity), &brush_panel_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x101010, kUiChromeOpacity), &brush_overlay_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x49A1FF, 1.0f), &brush_accent_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.14f), &brush_hover_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0xE81123, 0.82f), &brush_close_hover_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x111111, kViewportLetterboxOpacity), &brush_viewport_bg_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x111111, 1.0f), &brush_image_bg_);
    d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0x6D7685, kThumbnailCellOpacity), &brush_thumb_bg_);
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
    brush_viewport_bg_.Reset();
    brush_image_bg_.Reset();
    brush_thumb_bg_.Reset();
}

void QmiApp::ResetView() {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
}

void QmiApp::ClearAnimationState() {
    if (hwnd_) {
        KillTimer(hwnd_, kAnimationTimerId);
    }
    animation_frames_.clear();
    animation_frame_delays_ms_.clear();
    animation_frame_index_ = 0;
}

void QmiApp::ScheduleNextAnimationFrame() {
    if (!hwnd_ || animation_frames_.size() <= 1 || animation_frame_delays_ms_.size() != animation_frames_.size()) {
        return;
    }
    const UINT delay = std::max<UINT>(1u, animation_frame_delays_ms_[animation_frame_index_]);
    SetTimer(hwnd_, kAnimationTimerId, delay, nullptr);
}

void QmiApp::BuildDirectoryList(const fs::path& selected_file) {
    images_.clear();
    current_index_ = -1;
    film_strip_scroll_index_ = -1;

    std::error_code ec;
    fs::path target = fs::absolute(selected_file, ec);
    if (ec) {
        target = selected_file;
    }
    const fs::path directory = target.parent_path();
    const std::wstring target_name_key = ToLower(target.filename().wstring());

    struct DirectoryImageEntry {
        fs::path path;
        std::wstring sort_key;
        bool is_target = false;
    };

    std::vector<DirectoryImageEntry> found;
    found.reserve(256);

    std::error_code iter_ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, iter_ec);
    fs::directory_iterator end;
    if (!iter_ec) {
        for (; it != end; it.increment(iter_ec)) {
            if (iter_ec) {
                break;
            }
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || file_ec) {
                continue;
            }
            const fs::path entry_path = it->path();
            if (!IsSupportedExtension(entry_path)) {
                continue;
            }
            std::wstring name_key = ToLower(entry_path.filename().wstring());
            found.push_back(DirectoryImageEntry{entry_path, std::move(name_key), false});
            found.back().is_target = (found.back().sort_key == target_name_key);
        }
    }

    if (found.empty()) {
        found.push_back(DirectoryImageEntry{target, target_name_key, true});
    }

    std::sort(found.begin(), found.end(), [](const DirectoryImageEntry& a, const DirectoryImageEntry& b) {
        return a.sort_key < b.sort_key;
    });

    images_.reserve(found.size());
    for (int i = 0; i < static_cast<int>(found.size()); ++i) {
        images_.push_back(found[i].path);
        if (current_index_ < 0 && found[i].is_target) {
            current_index_ = i;
        }
    }

    if (current_index_ < 0 && !images_.empty()) {
        current_index_ = 0;
    }

    thumbnails_.assign(images_.size(), Thumbnail{});
}

void QmiApp::ScheduleDeferredDirectoryBuild(const fs::path& selected_file) {
    if (!hwnd_) {
        return;
    }
    deferred_directory_target_norm_ = NormalizePathLower(selected_file);
    deferred_directory_build_pending_ = true;
    SetTimer(hwnd_, kStartupScanTimerId, 1, nullptr);
}

void QmiApp::TryOpenInitialImage(const std::optional<std::wstring>& startup_path) {
    if (startup_path && !startup_path->empty()) {
        if (OpenImagePath(*startup_path, true, true)) {
            return;
        }
    }

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) {
        return;
    }
    if (const std::optional<fs::path> first_supported = FindFirstSupportedImageInDirectory(cwd); first_supported) {
        OpenImagePath(*first_supported, true, true);
    }
}

HRESULT QmiApp::LoadWebpBitmap(const fs::path& path,
                               UINT max_width,
                               UINT max_height,
                               ID2D1Bitmap1** out_bitmap,
                               float* out_width,
                               float* out_height) {
    if (!out_bitmap || !d2d_context_) {
        return E_POINTER;
    }

    *out_bitmap = nullptr;
    if (out_width) {
        *out_width = 0.0f;
    }
    if (out_height) {
        *out_height = 0.0f;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    const std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        return E_FAIL;
    }

    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(file_size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(encoded.data()), file_size)) {
        return E_FAIL;
    }

    int src_w = 0;
    int src_h = 0;
    if (!WebPGetInfo(encoded.data(), encoded.size(), &src_w, &src_h) || src_w <= 0 || src_h <= 0) {
        return E_FAIL;
    }

    int target_w = src_w;
    int target_h = src_h;
    if (max_width > 0 && max_height > 0 &&
        (static_cast<UINT>(src_w) > max_width || static_cast<UINT>(src_h) > max_height)) {
        const double ratio = std::min(static_cast<double>(max_width) / static_cast<double>(src_w),
                                      static_cast<double>(max_height) / static_cast<double>(src_h));
        target_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_w) * ratio)));
        target_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_h) * ratio)));
    }

    WebPDecoderConfig config{};
    if (!WebPInitDecoderConfig(&config)) {
        return E_FAIL;
    }

    const VP8StatusCode features_status = WebPGetFeatures(encoded.data(), encoded.size(), &config.input);
    if (features_status != VP8_STATUS_OK) {
        return E_FAIL;
    }

    config.output.colorspace = MODE_BGRA;
    if (target_w != src_w || target_h != src_h) {
        config.options.use_scaling = 1;
        config.options.scaled_width = target_w;
        config.options.scaled_height = target_h;
    }

    const VP8StatusCode decode_status = WebPDecode(encoded.data(), encoded.size(), &config);
    if (decode_status != VP8_STATUS_OK) {
        WebPFreeDecBuffer(&config.output);
        return E_FAIL;
    }

    const int decoded_w = config.output.width;
    const int decoded_h = config.output.height;
    const auto* decoded_pixels = config.output.u.RGBA.rgba;
    const int decoded_stride = config.output.u.RGBA.stride;
    if (!decoded_pixels || decoded_w <= 0 || decoded_h <= 0 || decoded_stride < decoded_w * 4) {
        WebPFreeDecBuffer(&config.output);
        return E_FAIL;
    }

    std::vector<std::uint8_t> premul_bgra(static_cast<std::size_t>(decoded_w) * static_cast<std::size_t>(decoded_h) * 4);
    for (int y = 0; y < decoded_h; ++y) {
        const auto* src_row = decoded_pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(decoded_stride);
        auto* dst_row = premul_bgra.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(decoded_w) * 4;
        for (int x = 0; x < decoded_w; ++x) {
            const std::uint8_t b = src_row[x * 4 + 0];
            const std::uint8_t g = src_row[x * 4 + 1];
            const std::uint8_t r = src_row[x * 4 + 2];
            const std::uint8_t a = src_row[x * 4 + 3];
            if (a == 255) {
                dst_row[x * 4 + 0] = b;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = r;
            } else {
                dst_row[x * 4 + 0] = static_cast<std::uint8_t>((static_cast<unsigned>(b) * a + 127u) / 255u);
                dst_row[x * 4 + 1] = static_cast<std::uint8_t>((static_cast<unsigned>(g) * a + 127u) / 255u);
                dst_row[x * 4 + 2] = static_cast<std::uint8_t>((static_cast<unsigned>(r) * a + 127u) / 255u);
            }
            dst_row[x * 4 + 3] = a;
        }
    }

    WebPFreeDecBuffer(&config.output);

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const HRESULT hr = d2d_context_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(decoded_w), static_cast<UINT32>(decoded_h)),
                                                  premul_bgra.data(),
                                                  static_cast<UINT32>(decoded_w * 4),
                                                  &props,
                                                  out_bitmap);
    if (FAILED(hr)) {
        return hr;
    }

    if (out_width) {
        *out_width = static_cast<float>(decoded_w);
    }
    if (out_height) {
        *out_height = static_cast<float>(decoded_h);
    }
    return S_OK;
}

HRESULT QmiApp::LoadRasterBitmap(const fs::path& path,
                                 UINT max_width,
                                 UINT max_height,
                                 ID2D1Bitmap1** out_bitmap,
                                 float* out_width,
                                 float* out_height) {
    if (!out_bitmap || !d2d_context_) {
        return E_POINTER;
    }

    if (IsWebpExtension(path)) {
        return LoadWebpBitmap(path, max_width, max_height, out_bitmap, out_width, out_height);
    }

    if (!wic_factory_) {
        return E_POINTER;
    }

    *out_bitmap = nullptr;
    if (out_width) {
        *out_width = 0.0f;
    }
    if (out_height) {
        *out_height = 0.0f;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic_factory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        return hr;
    }

    UINT src_w = 0;
    UINT src_h = 0;
    frame->GetSize(&src_w, &src_h);
    if (src_w == 0 || src_h == 0) {
        return E_FAIL;
    }

    ComPtr<IWICBitmapSource> source = frame;
    UINT out_w = src_w;
    UINT out_h = src_h;

    if (max_width > 0 && max_height > 0 && (src_w > max_width || src_h > max_height)) {
        const double ratio = std::min(static_cast<double>(max_width) / static_cast<double>(src_w),
                                      static_cast<double>(max_height) / static_cast<double>(src_h));
        out_w = std::max(1u, static_cast<UINT>(std::lround(src_w * ratio)));
        out_h = std::max(1u, static_cast<UINT>(std::lround(src_h * ratio)));

        ComPtr<IWICBitmapScaler> scaler;
        hr = wic_factory_->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) {
            return hr;
        }
        hr = scaler->Initialize(source.Get(), out_w, out_h, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return hr;
        }
        source = scaler;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = wic_factory_->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return hr;
    }

    hr = converter->Initialize(source.Get(),
                               GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone,
                               nullptr,
                               0.0f,
                               WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        return hr;
    }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = d2d_context_->CreateBitmapFromWicBitmap(converter.Get(), &props, out_bitmap);
    if (FAILED(hr)) {
        return hr;
    }

    if (out_width) {
        *out_width = static_cast<float>(out_w);
    }
    if (out_height) {
        *out_height = static_cast<float>(out_h);
    }
    return S_OK;
}

HRESULT QmiApp::LoadGifAnimation(const fs::path& path, LoadedImage* out_image) {
    if (!out_image || !d2d_context_ || !wic_factory_) {
        return E_POINTER;
    }

    animation_frames_.clear();
    animation_frame_delays_ms_.clear();
    animation_frame_index_ = 0;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic_factory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    UINT frame_count = 0;
    hr = decoder->GetFrameCount(&frame_count);
    if (FAILED(hr) || frame_count == 0) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    UINT canvas_width = 0;
    UINT canvas_height = 0;
    ComPtr<IWICMetadataQueryReader> decoder_reader;
    if (SUCCEEDED(decoder->GetMetadataQueryReader(&decoder_reader)) && decoder_reader) {
        TryReadMetadataUInt(decoder_reader.Get(), L"/logscrdesc/Width", &canvas_width);
        TryReadMetadataUInt(decoder_reader.Get(), L"/logscrdesc/Height", &canvas_height);
    }

    if (canvas_width == 0 || canvas_height == 0) {
        ComPtr<IWICBitmapFrameDecode> first_frame;
        hr = decoder->GetFrame(0, &first_frame);
        if (FAILED(hr) || !first_frame) {
            return FAILED(hr) ? hr : E_FAIL;
        }
        hr = first_frame->GetSize(&canvas_width, &canvas_height);
        if (FAILED(hr) || canvas_width == 0 || canvas_height == 0) {
            return FAILED(hr) ? hr : E_FAIL;
        }
    }

    const size_t canvas_stride = static_cast<size_t>(canvas_width) * 4u;
    if (canvas_width != 0 && canvas_stride / 4u != canvas_width) {
        return E_FAIL;
    }
    if (canvas_height != 0 && canvas_stride > std::numeric_limits<size_t>::max() / static_cast<size_t>(canvas_height)) {
        return E_FAIL;
    }
    const size_t canvas_size = canvas_stride * static_cast<size_t>(canvas_height);
    if (canvas_stride > std::numeric_limits<UINT32>::max() || canvas_size == 0) {
        return E_FAIL;
    }

    std::vector<std::uint8_t> canvas(canvas_size, 0);
    std::vector<std::uint8_t> restore_canvas;
    bool has_restore_canvas = false;

    UINT prev_disposal = 0;
    UINT prev_left = 0;
    UINT prev_top = 0;
    UINT prev_width = 0;
    UINT prev_height = 0;

    D2D1_BITMAP_PROPERTIES1 bitmap_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    animation_frames_.reserve(frame_count);
    animation_frame_delays_ms_.reserve(frame_count);

    for (UINT i = 0; i < frame_count; ++i) {
        if (i > 0) {
            if (prev_disposal == 2) {
                ClearCanvasRect(canvas, canvas_width, canvas_height, prev_left, prev_top, prev_width, prev_height);
            } else if (prev_disposal == 3 && has_restore_canvas && restore_canvas.size() == canvas.size()) {
                canvas = restore_canvas;
            }
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(i, &frame);
        if (FAILED(hr) || !frame) {
            return FAILED(hr) ? hr : E_FAIL;
        }

        UINT decode_width = 0;
        UINT decode_height = 0;
        hr = frame->GetSize(&decode_width, &decode_height);
        if (FAILED(hr) || decode_width == 0 || decode_height == 0) {
            return FAILED(hr) ? hr : E_FAIL;
        }

        UINT frame_left = 0;
        UINT frame_top = 0;
        UINT frame_rect_width = decode_width;
        UINT frame_rect_height = decode_height;
        UINT frame_disposal = 0;
        UINT frame_delay_cs = 0;

        ComPtr<IWICMetadataQueryReader> frame_reader;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&frame_reader)) && frame_reader) {
            TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Left", &frame_left);
            TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Top", &frame_top);
            UINT metadata_width = 0;
            UINT metadata_height = 0;
            if (TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Width", &metadata_width) && metadata_width > 0) {
                frame_rect_width = metadata_width;
            }
            if (TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Height", &metadata_height) && metadata_height > 0) {
                frame_rect_height = metadata_height;
            }
            TryReadMetadataUInt(frame_reader.Get(), L"/grctlext/Disposal", &frame_disposal);
            TryReadMetadataUInt(frame_reader.Get(), L"/grctlext/Delay", &frame_delay_cs);
        }

        const bool needs_restore = frame_disposal == 3;
        if (needs_restore) {
            restore_canvas = canvas;
            has_restore_canvas = true;
        } else {
            has_restore_canvas = false;
        }

        ComPtr<IWICFormatConverter> converter;
        hr = wic_factory_->CreateFormatConverter(&converter);
        if (FAILED(hr) || !converter) {
            return FAILED(hr) ? hr : E_FAIL;
        }
        hr = converter->Initialize(frame.Get(),
                                   GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0f,
                                   WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) {
            return hr;
        }

        const size_t frame_stride = static_cast<size_t>(decode_width) * 4u;
        if (decode_width != 0 && frame_stride / 4u != decode_width) {
            return E_FAIL;
        }
        if (decode_height != 0 && frame_stride > std::numeric_limits<size_t>::max() / static_cast<size_t>(decode_height)) {
            return E_FAIL;
        }
        const size_t frame_size = frame_stride * static_cast<size_t>(decode_height);
        if (frame_stride > std::numeric_limits<UINT>::max() || frame_size > std::numeric_limits<UINT>::max() || frame_size == 0) {
            return E_FAIL;
        }

        std::vector<std::uint8_t> frame_pixels(frame_size, 0);
        hr = converter->CopyPixels(
            nullptr, static_cast<UINT>(frame_stride), static_cast<UINT>(frame_size), frame_pixels.data());
        if (FAILED(hr)) {
            return hr;
        }

        CompositeFrame(canvas, canvas_width, canvas_height, frame_pixels, decode_width, decode_height, frame_left, frame_top);

        ComPtr<ID2D1Bitmap1> frame_bitmap;
        hr = d2d_context_->CreateBitmap(D2D1::SizeU(canvas_width, canvas_height),
                                        canvas.data(),
                                        static_cast<UINT32>(canvas_stride),
                                        &bitmap_props,
                                        &frame_bitmap);
        if (FAILED(hr) || !frame_bitmap) {
            return FAILED(hr) ? hr : E_FAIL;
        }

        ULONGLONG delay_ms = frame_delay_cs > 0 ? static_cast<ULONGLONG>(frame_delay_cs) * 10ull : kGifDefaultDelayMs;
        delay_ms = std::max<ULONGLONG>(delay_ms, kGifMinDelayMs);
        if (delay_ms > std::numeric_limits<UINT>::max()) {
            delay_ms = std::numeric_limits<UINT>::max();
        }

        animation_frames_.push_back(frame_bitmap);
        animation_frame_delays_ms_.push_back(static_cast<UINT>(delay_ms));

        prev_disposal = frame_disposal;
        prev_left = frame_left;
        prev_top = frame_top;
        prev_width = frame_rect_width;
        prev_height = frame_rect_height;
    }

    if (animation_frames_.empty()) {
        return E_FAIL;
    }

    out_image->type = ImageType::Raster;
    out_image->raster = animation_frames_[0];
    out_image->width = static_cast<float>(canvas_width);
    out_image->height = static_cast<float>(canvas_height);
    animation_frame_index_ = 0;
    return S_OK;
}

HRESULT QmiApp::LoadSvgDocument(const fs::path& path, ID2D1SvgDocument** out_svg, float* out_width, float* out_height) {
    if (!out_svg || !d2d_context5_) {
        return E_NOINTERFACE;
    }

    *out_svg = nullptr;
    if (out_width) {
        *out_width = 0.0f;
    }
    if (out_height) {
        *out_height = 0.0f;
    }

    ComPtr<IStream> stream;
    HRESULT hr = SHCreateStreamOnFileEx(
        path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<ID2D1SvgDocument> doc;
    hr = d2d_context5_->CreateSvgDocument(stream.Get(), D2D1::SizeF(1024.0f, 1024.0f), &doc);
    if (FAILED(hr)) {
        return hr;
    }

    const D2D1_SIZE_F viewport = doc->GetViewportSize();
    float width = viewport.width;
    float height = viewport.height;
    if (width < 1.0f || height < 1.0f) {
        width = 1024.0f;
        height = 1024.0f;
        doc->SetViewportSize(D2D1::SizeF(width, height));
    }

    if (out_width) {
        *out_width = width;
    }
    if (out_height) {
        *out_height = height;
    }

    *out_svg = doc.Detach();
    return S_OK;
}

HRESULT QmiApp::LoadSvgThumbnailBitmap(const fs::path& path,
                                       UINT max_width,
                                       UINT max_height,
                                       ID2D1Bitmap1** out_bitmap,
                                       float* out_width,
                                       float* out_height) {
    if (!out_bitmap || !d2d_context_ || !d2d_context5_) {
        return E_POINTER;
    }

    *out_bitmap = nullptr;
    if (out_width) {
        *out_width = 0.0f;
    }
    if (out_height) {
        *out_height = 0.0f;
    }

    ComPtr<ID2D1SvgDocument> svg;
    float svg_width = 0.0f;
    float svg_height = 0.0f;
    HRESULT hr = LoadSvgDocument(path, &svg, &svg_width, &svg_height);
    if (FAILED(hr) || !svg) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    float bitmap_width = std::max(1.0f, svg_width);
    float bitmap_height = std::max(1.0f, svg_height);
    if (max_width > 0 && max_height > 0 && (bitmap_width > static_cast<float>(max_width) ||
                                             bitmap_height > static_cast<float>(max_height))) {
        const float ratio =
            std::min(static_cast<float>(max_width) / bitmap_width, static_cast<float>(max_height) / bitmap_height);
        bitmap_width = std::max(1.0f, std::round(bitmap_width * ratio));
        bitmap_height = std::max(1.0f, std::round(bitmap_height * ratio));
    }

    const UINT32 target_w = static_cast<UINT32>(bitmap_width);
    const UINT32 target_h = static_cast<UINT32>(bitmap_height);
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = d2d_context_->CreateBitmap(D2D1::SizeU(target_w, target_h), nullptr, 0, &props, &bitmap);
    if (FAILED(hr) || !bitmap) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ComPtr<ID2D1Image> previous_target;
    d2d_context_->GetTarget(&previous_target);
    D2D1_MATRIX_3X2_F previous_transform{};
    d2d_context_->GetTransform(&previous_transform);

    d2d_context_->SetTarget(bitmap.Get());
    d2d_context_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d_context_->Clear(D2D1::ColorF(0x000000, 0.0f));

    const float scale_x = bitmap_width / std::max(1.0f, svg_width);
    const float scale_y = bitmap_height / std::max(1.0f, svg_height);
    const float draw_scale = std::min(scale_x, scale_y);
    const float draw_w = svg_width * draw_scale;
    const float draw_h = svg_height * draw_scale;
    const float offset_x = (bitmap_width - draw_w) * 0.5f;
    const float offset_y = (bitmap_height - draw_h) * 0.5f;

    d2d_context_->SetTransform(
        D2D1::Matrix3x2F::Scale(draw_scale, draw_scale, D2D1::Point2F(0.0f, 0.0f)) *
        D2D1::Matrix3x2F::Translation(offset_x, offset_y));
    d2d_context5_->DrawSvgDocument(svg.Get());

    d2d_context_->SetTransform(previous_transform);
    d2d_context_->SetTarget(previous_target.Get());

    if (out_width) {
        *out_width = bitmap_width;
    }
    if (out_height) {
        *out_height = bitmap_height;
    }
    *out_bitmap = bitmap.Detach();
    return S_OK;
}

bool QmiApp::LoadImageByIndex(int index, bool reset_view) {
    if (index < 0 || index >= static_cast<int>(images_.size())) {
        return false;
    }
    if (!d2d_context_) {
        return false;
    }

    ClearAnimationState();

    const fs::path path = images_[index];
    const std::wstring ext = ToLower(path.extension().wstring());

    LoadedImage image;
    image.path = path;

    HRESULT hr = E_FAIL;
    if (ext == L".svg") {
        ComPtr<ID2D1SvgDocument> svg;
        hr = LoadSvgDocument(path, &svg, &image.width, &image.height);
        if (SUCCEEDED(hr) && svg) {
            image.type = ImageType::Svg;
            image.svg = svg;
        }
    } else {
        if (IsGifExtension(path)) {
            hr = LoadGifAnimation(path, &image);
        } else {
            ComPtr<ID2D1Bitmap1> bmp;
            hr = LoadRasterBitmap(path, 0, 0, &bmp, &image.width, &image.height);
            if (SUCCEEDED(hr) && bmp) {
                image.type = ImageType::Raster;
                image.raster = bmp;
            }
        }
    }

    if (FAILED(hr) || image.type == ImageType::None) {
        current_error_ = L"Unable to decode this image.";
        return false;
    }

    current_image_ = std::move(image);
    if (index != current_index_) {
        film_strip_scroll_index_ = -1;
    }
    current_index_ = index;
    current_error_.clear();
    hover_open_button_ = false;
    pressed_open_button_ = false;

    if (reset_view && fit_on_switch_) {
        ResetView();
    }

    if (animation_frames_.size() > 1) {
        ScheduleNextAnimationFrame();
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool QmiApp::OpenImagePath(const fs::path& path, bool reset_view, bool defer_directory_scan) {
    if (path.empty() || !IsSupportedExtension(path)) {
        current_error_ = L"Unsupported file format.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (defer_directory_scan) {
        images_.clear();
        images_.push_back(path);
        current_index_ = 0;
        thumbnails_.assign(1, Thumbnail{});
        film_strip_scroll_index_ = -1;
    } else {
        BuildDirectoryList(path);
    }

    if (images_.empty()) {
        current_error_ = L"No supported images in this folder.";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (current_index_ < 0 || current_index_ >= static_cast<int>(images_.size())) {
        current_index_ = 0;
    }

    if (!LoadImageByIndex(current_index_, reset_view)) {
        return false;
    }

    if (defer_directory_scan) {
        ScheduleDeferredDirectoryBuild(path);
    }

    SetWindowTextW(hwnd_, (L"Qmi - " + current_image_.path.filename().wstring()).c_str());
    return true;
}

void QmiApp::MoveSelection(int delta) {
    if (images_.empty() || current_index_ < 0) {
        return;
    }

    int next = current_index_ + delta;
    if (next < 0) {
        next = static_cast<int>(images_.size()) - 1;
    } else if (next >= static_cast<int>(images_.size())) {
        next = 0;
    }
    LoadImageByIndex(next, true);
}

void QmiApp::EnsureThumbnailLoaded(int index) {
    if (index < 0 || index >= static_cast<int>(thumbnails_.size())) {
        return;
    }

    Thumbnail& thumb = thumbnails_[index];
    if (thumb.attempted) {
        return;
    }
    thumb.attempted = true;

    const fs::path& path = images_[index];
    ComPtr<ID2D1Bitmap1> bitmap;
    HRESULT hr = E_FAIL;
    if (ToLower(path.extension().wstring()) == L".svg") {
        hr = LoadSvgThumbnailBitmap(path, 220, 160, &bitmap, &thumb.width, &thumb.height);
    } else {
        hr = LoadRasterBitmap(path, 220, 160, &bitmap, &thumb.width, &thumb.height);
    }
    if (FAILED(hr) || !bitmap) {
        thumb.failed = true;
        return;
    }
    thumb.bitmap = bitmap;
}

D2D1_RECT_F QmiApp::GetFilmStripRect(float width, float height) const {
    const float strip_top = std::max(kViewportMargin + 70.0f, height - static_cast<float>(film_strip_height_));
    return D2D1::RectF(0.0f, strip_top, width, height);
}

D2D1_RECT_F QmiApp::GetImageViewport(float width, float height) const {
    const D2D1_RECT_F strip = GetFilmStripRect(width, height);
    const float top = kViewportMargin;
    const float bottom = strip.top - kViewportBottomGap;
    const float left = kViewportMargin;
    const float right = width - kViewportMargin;
    if (bottom <= top + 40.0f) {
        return D2D1::RectF(left, top, right, top + 40.0f);
    }
    return D2D1::RectF(left, top, right, bottom);
}

float QmiApp::GetBaseImageScale(const D2D1_RECT_F& viewport) const {
    if (current_image_.type == ImageType::None) {
        return 1.0f;
    }

    const float img_w = std::max(1.0f, current_image_.width);
    const float img_h = std::max(1.0f, current_image_.height);
    const float view_w = std::max(1.0f, viewport.right - viewport.left);
    const float view_h = std::max(1.0f, viewport.bottom - viewport.top);
    const float fit = std::min(view_w / img_w, view_h / img_h);
    return std::min(1.0f, fit);
}

D2D1_RECT_F QmiApp::GetImageDestinationRect(const D2D1_RECT_F& viewport) const {
    if (current_image_.type == ImageType::None) {
        return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const float img_w = std::max(1.0f, current_image_.width);
    const float img_h = std::max(1.0f, current_image_.height);
    const float scale = std::max(0.02f, GetBaseImageScale(viewport) * zoom_);
    const float dest_w = img_w * scale;
    const float dest_h = img_h * scale;

    const float cx = (viewport.left + viewport.right) * 0.5f + pan_x_;
    const float cy = (viewport.top + viewport.bottom) * 0.5f + pan_y_;
    return D2D1::RectF(cx - dest_w * 0.5f, cy - dest_h * 0.5f, cx + dest_w * 0.5f, cy + dest_h * 0.5f);
}

D2D1_RECT_F QmiApp::GetOpenButtonRect(const D2D1_RECT_F& viewport) const {
    const float view_w = std::max(1.0f, viewport.right - viewport.left);
    const float view_h = std::max(1.0f, viewport.bottom - viewport.top);
    const float width = Clamp(view_w * 0.34f, 180.0f, 260.0f);
    const float height = std::min(56.0f, std::max(42.0f, view_h * 0.18f));
    const float cx = (viewport.left + viewport.right) * 0.5f;
    const float cy = (viewport.top + viewport.bottom) * 0.5f;
    return D2D1::RectF(cx - width * 0.5f, cy - height * 0.5f, cx + width * 0.5f, cy + height * 0.5f);
}

TitleButtons QmiApp::GetTitleButtons(const D2D1_RECT_F& viewport) const {
    TitleButtons b{};

    const int top = static_cast<int>(std::lround(viewport.top + 10.0f));
    const int right = static_cast<int>(std::lround(viewport.right - 10.0f));

    b.close_rect = RECT{right - kTitleButtonWidth, top, right, top + kTitleButtonHeight};
    b.max_rect = RECT{right - kTitleButtonWidth * 2, top, right - kTitleButtonWidth, top + kTitleButtonHeight};
    b.min_rect = RECT{right - kTitleButtonWidth * 3, top, right - kTitleButtonWidth * 2, top + kTitleButtonHeight};

    const int min_left = static_cast<int>(std::lround(viewport.left + 10.0f));
    if (b.min_rect.left < min_left) {
        const int shift = min_left - b.min_rect.left;
        OffsetRect(&b.min_rect, shift, 0);
        OffsetRect(&b.max_rect, shift, 0);
        OffsetRect(&b.close_rect, shift, 0);
    }

    return b;
}

TitleButton QmiApp::HitTestTitleButton(POINT client_pt) const {
    if (!hwnd_) {
        return TitleButton::None;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F viewport = GetImageViewport(static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
    const TitleButtons buttons = GetTitleButtons(viewport);

    if (PtInRect(&buttons.close_rect, client_pt)) {
        return TitleButton::Close;
    }
    if (PtInRect(&buttons.max_rect, client_pt)) {
        return TitleButton::Maximize;
    }
    if (PtInRect(&buttons.min_rect, client_pt)) {
        return TitleButton::Minimize;
    }
    return TitleButton::None;
}

bool QmiApp::HitTestOpenButton(POINT client_pt) const {
    if (!hwnd_ || current_image_.type != ImageType::None) {
        return false;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F viewport = GetImageViewport(static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
    const D2D1_RECT_F button_rect = GetOpenButtonRect(viewport);

    const float x = static_cast<float>(client_pt.x);
    const float y = static_cast<float>(client_pt.y);
    return x >= button_rect.left && x <= button_rect.right && y >= button_rect.top && y <= button_rect.bottom;
}

bool QmiApp::IsPointOverVisibleImage(POINT client_pt, const D2D1_RECT_F& viewport) const {
    if (current_image_.type == ImageType::None) {
        return false;
    }

    const D2D1_RECT_F dest = GetImageDestinationRect(viewport);
    const D2D1_RECT_F visible = D2D1::RectF(std::max(dest.left, viewport.left),
                                             std::max(dest.top, viewport.top),
                                             std::min(dest.right, viewport.right),
                                             std::min(dest.bottom, viewport.bottom));
    if (visible.right <= visible.left || visible.bottom <= visible.top) {
        return false;
    }

    const float x = static_cast<float>(client_pt.x);
    const float y = static_cast<float>(client_pt.y);
    return x >= visible.left && x <= visible.right && y >= visible.top && y <= visible.bottom;
}

int QmiApp::HitTestThumbnail(POINT client_pt) const {
    for (const auto& thumb : visible_thumbs_) {
        if (client_pt.x >= thumb.rect.left && client_pt.x <= thumb.rect.right && client_pt.y >= thumb.rect.top &&
            client_pt.y <= thumb.rect.bottom) {
            return thumb.index;
        }
    }
    return -1;
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
    const float s = std::max(8.0f, w * 0.17f);

    switch (button) {
        case TitleButton::Minimize: {
            d2d_context_->DrawLine(D2D1::Point2F(cx - s, cy + s * 0.4f),
                                   D2D1::Point2F(cx + s, cy + s * 0.4f),
                                   brush_text_.Get(),
                                   1.6f);
            break;
        }
        case TitleButton::Maximize: {
            const bool maximized = IsZoomed(hwnd_) != FALSE;
            if (!maximized) {
                d2d_context_->DrawRectangle(
                    D2D1::RectF(cx - s, cy - s, cx + s, cy + s), brush_text_.Get(), 1.5f);
            } else {
                d2d_context_->DrawRectangle(
                    D2D1::RectF(cx - s + 2.5f, cy - s + 1.5f, cx + s + 2.5f, cy + s + 1.5f), brush_text_.Get(), 1.2f);
                d2d_context_->DrawRectangle(
                    D2D1::RectF(cx - s - 2.0f, cy - s - 1.0f, cx + s - 2.0f, cy + s - 1.0f), brush_text_.Get(), 1.2f);
            }
            break;
        }
        case TitleButton::Close: {
            d2d_context_->DrawLine(D2D1::Point2F(cx - s, cy - s),
                                   D2D1::Point2F(cx + s, cy + s),
                                   brush_text_.Get(),
                                   1.6f);
            d2d_context_->DrawLine(D2D1::Point2F(cx + s, cy - s),
                                   D2D1::Point2F(cx - s, cy + s),
                                   brush_text_.Get(),
                                   1.6f);
            break;
        }
        default:
            break;
    }
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

void QmiApp::DrawOpenButton(const D2D1_RECT_F& viewport) {
    if (!d2d_context_ || !brush_panel_ || !brush_text_) {
        return;
    }
    if (current_image_.type != ImageType::None) {
        return;
    }

    const D2D1_RECT_F button_rect = GetOpenButtonRect(viewport);
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(button_rect, 10.0f, 10.0f);

    d2d_context_->FillRoundedRectangle(rounded, brush_panel_.Get());
    if (hover_open_button_ && brush_hover_) {
        d2d_context_->FillRoundedRectangle(rounded, brush_hover_.Get());
    }

    if (pressed_open_button_ && brush_accent_) {
        d2d_context_->DrawRoundedRectangle(rounded, brush_accent_.Get(), 2.0f);
    } else if (brush_overlay_) {
        d2d_context_->DrawRoundedRectangle(rounded, brush_overlay_.Get(), 1.0f);
    }

    DrawCenteredText(L"Open image...", button_rect, text_format_.Get());
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
        const D2D1_RECT_F button_rect = GetOpenButtonRect(viewport);
        const float hint_top = std::min(viewport.bottom - 38.0f, button_rect.bottom + 12.0f);
        DrawCenteredText(L"Or drag and drop an image file.", D2D1::RectF(viewport.left + 20.0f, hint_top, viewport.right - 20.0f, hint_top + 26.0f), small_text_format_.Get());
        return;
    }

    const D2D1_RECT_F dest = GetImageDestinationRect(viewport);
    const float img_w = std::max(1.0f, current_image_.width);
    const float img_h = std::max(1.0f, current_image_.height);
    const float dest_w = std::max(1.0f, dest.right - dest.left);
    const float dest_h = std::max(1.0f, dest.bottom - dest.top);
    const float scale = std::min(dest_w / img_w, dest_h / img_h);

    d2d_context_->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (current_image_.type == ImageType::Raster && current_image_.raster) {
        const D2D1_INTERPOLATION_MODE interp =
            smooth_sampling_ ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        d2d_context_->DrawBitmap(current_image_.raster.Get(), dest, 1.0f, interp);
    } else if (current_image_.type == ImageType::Svg && current_image_.svg && d2d_context5_) {
        D2D1_MATRIX_3X2_F prev{};
        d2d_context_->GetTransform(&prev);
        d2d_context_->SetTransform(
            D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(0.0f, 0.0f)) * D2D1::Matrix3x2F::Translation(dest.left, dest.top));
        d2d_context5_->DrawSvgDocument(current_image_.svg.Get());
        d2d_context_->SetTransform(prev);
    }

    d2d_context_->PopAxisAlignedClip();
}

void QmiApp::DrawFilmStrip(const D2D1_RECT_F& strip_rect) {
    if (!d2d_context_ || !brush_panel_ || !brush_thumb_bg_) {
        return;
    }

    d2d_context_->FillRectangle(strip_rect, brush_panel_.Get());

    visible_thumbs_.clear();
    if (images_.empty()) {
        return;
    }

    const float padding = 12.0f;
    const float gap = 10.0f;
    const float available_h = std::max(40.0f, (strip_rect.bottom - strip_rect.top) - 2.0f * padding);
    const float item_h = available_h;
    const float item_w = item_h * 1.3f;
    const int slots = std::max(1, static_cast<int>((strip_rect.right - strip_rect.left - padding * 2.0f + gap) / (item_w + gap)));
    const int max_start = std::max(0, static_cast<int>(images_.size()) - slots);

    int start = 0;
    if (film_strip_scroll_index_ >= 0) {
        start = Clamp(film_strip_scroll_index_, 0, max_start);
    } else if (current_index_ >= 0) {
        start = Clamp(current_index_ - slots / 2, 0, max_start);
    }

    const int end = std::min(static_cast<int>(images_.size()), start + slots);
    float x = strip_rect.left + padding;
    int remaining_decode_budget = kThumbnailDecodeBudgetPerFrame;
    bool has_pending_visible_thumbnail = false;

    if (current_index_ >= start && current_index_ < end && current_index_ < static_cast<int>(thumbnails_.size()) &&
        !thumbnails_[current_index_].attempted && remaining_decode_budget > 0) {
        EnsureThumbnailLoaded(current_index_);
        --remaining_decode_budget;
    }

    for (int i = start; i < end; ++i) {
        D2D1_RECT_F cell = D2D1::RectF(x, strip_rect.top + padding, x + item_w, strip_rect.top + padding + item_h);
        visible_thumbs_.push_back(VisibleThumb{i, cell});

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
            std::wstring label = ext.empty() ? L"IMG" : ext.substr(1);
            DrawCenteredText(label, cell, small_text_format_.Get());
        }

        const float stroke = (i == current_index_) ? 3.0f : 1.0f;
        ID2D1SolidColorBrush* stroke_brush = (i == current_index_) ? brush_accent_.Get() : brush_overlay_.Get();
        d2d_context_->DrawRectangle(cell, stroke_brush, stroke);

        x += item_w + gap;
    }

    if (has_pending_visible_thumbnail) {
        RequestRender(true);
    }
}

void QmiApp::DrawMessageOverlay(const std::wstring& text) {
    if (!d2d_context_ || text.empty()) {
        return;
    }
    D2D1_SIZE_F size = d2d_context_->GetSize();
    DrawCenteredText(text, D2D1::RectF(20.0f, size.height - 36.0f, size.width - 20.0f, size.height - 8.0f), small_text_format_.Get());
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

    if (brush_overlay_) {
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, viewport.top), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, viewport.top, viewport.left, viewport.bottom), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(viewport.right, viewport.top, size.width, viewport.bottom), brush_overlay_.Get());
        d2d_context_->FillRectangle(D2D1::RectF(0.0f, viewport.bottom, size.width, strip.top), brush_overlay_.Get());
    }

    DrawFilmStrip(strip);
    DrawTitleButtons(GetTitleButtons(viewport));

    if (!current_error_.empty()) {
        DrawMessageOverlay(current_error_);
    }

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

void QmiApp::OpenFileDialog() {
    wchar_t file_path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.gif;*.svg\0All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"jpg";

    if (GetOpenFileNameW(&ofn)) {
        OpenImagePath(file_path, true);
    }
}

void QmiApp::OpenSettingsWindow() {
    if (settings_hwnd_ && IsWindow(settings_hwnd_)) {
        ShowWindow(settings_hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(settings_hwnd_);
        return;
    }

    settings_hwnd_ = CreateWindowExW(WS_EX_APPWINDOW,
                                     kSettingsClassName,
                                     L"Qmi \u8bbe\u7f6e",
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                     CW_USEDEFAULT,
                                     CW_USEDEFAULT,
                                     700,
                                     460,
                                     hwnd_,
                                     nullptr,
                                     hinstance_,
                                     this);
    if (settings_hwnd_) {
        ShowWindow(settings_hwnd_, SW_SHOWNORMAL);
        UpdateWindow(settings_hwnd_);
    }
}

void QmiApp::ShowContextMenu(POINT screen_pt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuOpenFile, L"Open image...");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuOpenFile:
            OpenFileDialog();
            break;
        case kMenuSettings:
            OpenSettingsWindow();
            break;
        case kMenuExit:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
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

void QmiApp::UpdateHoverState(POINT client_pt) {
    const TitleButton hovered = HitTestTitleButton(client_pt);
    const bool hovered_open_button = HitTestOpenButton(client_pt);
    if (hovered != hover_button_ || hovered_open_button != hover_open_button_) {
        hover_button_ = hovered;
        hover_open_button_ = hovered_open_button;
        RequestRender(true);
    }
}

void QmiApp::HandleMouseWheel(short wheel_delta, POINT screen_pt) {
    if (!d2d_context_) {
        return;
    }

    POINT client_pt = screen_pt;
    ScreenToClient(hwnd_, &client_pt);

    const D2D1_SIZE_F size = d2d_context_->GetSize();
    const D2D1_RECT_F strip = GetFilmStripRect(size.width, size.height);
    if (client_pt.x >= strip.left && client_pt.x <= strip.right && client_pt.y >= strip.top && client_pt.y <= strip.bottom) {
        if (images_.empty()) {
            return;
        }

        const float padding = 12.0f;
        const float gap = 10.0f;
        const float available_h = std::max(40.0f, (strip.bottom - strip.top) - 2.0f * padding);
        const float item_w = available_h * 1.3f;
        const int slots = std::max(
            1, static_cast<int>((strip.right - strip.left - padding * 2.0f + gap) / (item_w + gap)));
        const int max_start = std::max(0, static_cast<int>(images_.size()) - slots);
        if (max_start <= 0) {
            return;
        }

        int start = 0;
        if (film_strip_scroll_index_ >= 0) {
            start = Clamp(film_strip_scroll_index_, 0, max_start);
        } else if (current_index_ >= 0) {
            start = Clamp(current_index_ - slots / 2, 0, max_start);
        }

        int notches = static_cast<int>(wheel_delta / WHEEL_DELTA);
        if (notches == 0) {
            notches = (wheel_delta > 0) ? 1 : -1;
        }
        const int next = Clamp(start - notches, 0, max_start);
        film_strip_scroll_index_ = next;
        if (next != start || start != Clamp(current_index_ - slots / 2, 0, max_start)) {
            RequestRender(true);
        }
        return;
    }

    if (current_image_.type == ImageType::None) {
        return;
    }

    const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
    if (client_pt.x < viewport.left || client_pt.x > viewport.right || client_pt.y < viewport.top || client_pt.y > viewport.bottom) {
        return;
    }

    const float img_w = std::max(1.0f, current_image_.width);
    const float img_h = std::max(1.0f, current_image_.height);
    const float base_scale = GetBaseImageScale(viewport);
    const float old_scale = std::max(0.02f, base_scale * zoom_);

    const float center_x = (viewport.left + viewport.right) * 0.5f;
    const float center_y = (viewport.top + viewport.bottom) * 0.5f;
    const float image_left_before = center_x - img_w * old_scale * 0.5f + pan_x_;
    const float image_top_before = center_y - img_h * old_scale * 0.5f + pan_y_;
    const float image_space_x = (client_pt.x - image_left_before) / old_scale;
    const float image_space_y = (client_pt.y - image_top_before) / old_scale;

    const float factor = wheel_delta > 0 ? 1.12f : (1.0f / 1.12f);
    zoom_ = Clamp(zoom_ * factor, 0.05f, 40.0f);

    const float new_scale = std::max(0.02f, base_scale * zoom_);
    const float new_left = client_pt.x - image_space_x * new_scale;
    const float new_top = client_pt.y - image_space_y * new_scale;
    pan_x_ = new_left - (center_x - img_w * new_scale * 0.5f);
    pan_y_ = new_top - (center_y - img_h * new_scale * 0.5f);

    RequestRender(true);
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

        case WM_KEYDOWN:
            if (wparam == VK_RIGHT || wparam == VK_DOWN) {
                MoveSelection(1);
            } else if (wparam == VK_LEFT || wparam == VK_UP) {
                MoveSelection(-1);
            } else if (wparam == '0') {
                ResetView();
                RequestRender();
            }
            return 0;

        case WM_TIMER:
            if (wparam == kAnimationTimerId) {
                KillTimer(hwnd_, kAnimationTimerId);
                if (animation_frames_.size() <= 1 || animation_frame_delays_ms_.size() != animation_frames_.size() ||
                    current_image_.type != ImageType::Raster) {
                    return 0;
                }
                animation_frame_index_ = (animation_frame_index_ + 1) % animation_frames_.size();
                current_image_.raster = animation_frames_[animation_frame_index_];
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

            state->title_font = CreateFontW(-24, 0, 0, 0, 600, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            state->nav_font = CreateFontW(-18, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                          L"Segoe UI");
            state->body_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                           L"Segoe UI");

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
            if (state->nav_list) {
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5e38\u89c4"));
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5173\u8054"));
                SendMessageW(state->nav_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"\u5173\u4e8e"));
                SendMessageW(state->nav_list, LB_SETCURSEL, static_cast<WPARAM>(SettingsPage::General), 0);
            }

            state->general_title = CreateWindowExW(0,
                                                   L"STATIC",
                                                   L"\u5e38\u89c4",
                                                   WS_CHILD | WS_VISIBLE,
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   hwnd,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr);
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
            SendMessageW(state->fit_checkbox, BM_SETCHECK, app && app->fit_on_switch_ ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(state->smooth_checkbox,
                         BM_SETCHECK,
                         app && app->smooth_sampling_ ? BST_CHECKED : BST_UNCHECKED,
                         0);

            state->associations_title = CreateWindowExW(0,
                                                        L"STATIC",
                                                        L"\u5173\u8054",
                                                        WS_CHILD | WS_VISIBLE,
                                                        0,
                                                        0,
                                                        0,
                                                        0,
                                                        hwnd,
                                                        nullptr,
                                                        nullptr,
                                                        nullptr);
            state->associations_text = CreateWindowExW(
                0,
                L"STATIC",
                L"\u8fd9\u91cc\u7528\u4e8e\u7ba1\u7406\u56fe\u7247\u6587\u4ef6\u4e0e Qmi \u7684\u5173\u8054\u65b9\u5f0f\u3002\r\n\r\n\u5f53\u524d\u7248\u672c\u6682\u672a\u63d0\u4f9b\u53ef\u914d\u7f6e\u9879\u3002",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                0,
                0,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            state->about_title = CreateWindowExW(0,
                                                 L"STATIC",
                                                 L"\u5173\u4e8e",
                                                 WS_CHILD | WS_VISIBLE,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 hwnd,
                                                 nullptr,
                                                 nullptr,
                                                 nullptr);
            state->about_text = CreateWindowExW(0,
                                                L"STATIC",
                                                L"Qmi\r\n\r\n\u8f7b\u91cf\u7ea7 Windows \u770b\u56fe\u5de5\u5177\u3002\r\n\u652f\u6301\u683c\u5f0f\uff1ajpg / jpeg / png / bmp / webp / gif / svg",
                                                WS_CHILD | WS_VISIBLE,
                                                0,
                                                0,
                                                0,
                                                0,
                                                hwnd,
                                                nullptr,
                                                nullptr,
                                                nullptr);

            SetControlFont(state->nav_list, state->nav_font);
            SetControlFont(state->general_title, state->title_font);
            SetControlFont(state->fit_checkbox, state->body_font);
            SetControlFont(state->smooth_checkbox, state->body_font);
            SetControlFont(state->associations_title, state->title_font);
            SetControlFont(state->associations_text, state->body_font);
            SetControlFont(state->about_title, state->title_font);
            SetControlFont(state->about_text, state->body_font);

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
            if (app && HIWORD(wparam) == BN_CLICKED) {
                if (LOWORD(wparam) == kCtrlFitOnSwitch && lparam) {
                    app->fit_on_switch_ = SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0) == BST_CHECKED;
                } else if (LOWORD(wparam) == kCtrlSmoothSampling && lparam) {
                    app->smooth_sampling_ =
                        SendMessageW(reinterpret_cast<HWND>(lparam), BM_GETCHECK, 0, 0) == BST_CHECKED;
                }
                InvalidateRect(app->hwnd_, nullptr, FALSE);
            }
            return 0;
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
                const bool is_panel_ctrl = ctrl == state->general_title || ctrl == state->fit_checkbox ||
                                           ctrl == state->smooth_checkbox || ctrl == state->associations_title ||
                                           ctrl == state->associations_text || ctrl == state->about_title ||
                                           ctrl == state->about_text;
                if (is_panel_ctrl) {
                    const bool title_ctrl = ctrl == state->general_title || ctrl == state->associations_title ||
                                            ctrl == state->about_title;
                    SetTextColor(hdc, title_ctrl ? RGB(28, 34, 46) : RGB(52, 58, 70));
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
                if (state->title_font) {
                    DeleteObject(state->title_font);
                }
                if (state->nav_font) {
                    DeleteObject(state->nav_font);
                }
                if (state->body_font) {
                    DeleteObject(state->body_font);
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
        MessageBoxW(nullptr, L"Qmi initialization failed.", L"Qmi", MB_ICONERROR | MB_OK);
        return 1;
    }
    return app.Run();
}
