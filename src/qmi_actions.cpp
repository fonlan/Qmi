#include "qmi_app.h"

#include "qmi_settings.h"
#include "qmi_utils.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {
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
constexpr UINT_PTR kStartupScanTimerId = 2;

bool IsRenderableImageTypeForActions(ImageType type) {
    return type == ImageType::Raster || type == ImageType::Svg;
}
}  // namespace

void QmiApp::OpenFileDialog() {
    wchar_t file_path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"\u56fe\u7247\u6587\u4ef6\0*.jpg;*.jpeg;*.png;*.bmp;*.ico;*.webp;*.gif;*.heic;*.heif;*.svg\0\u6240\u6709\u6587\u4ef6\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"jpg";

    if (GetOpenFileNameW(&ofn)) {
        OpenImagePath(file_path, true);
    }
}

HRESULT QmiApp::ReadBitmapPixels(ID2D1Bitmap1* source_bitmap,
                                 UINT32* out_width,
                                 UINT32* out_height,
                                 std::vector<std::uint8_t>* out_pixels) {
    if (!source_bitmap || !d2d_context_ || !out_width || !out_height || !out_pixels) {
        return E_POINTER;
    }

    *out_width = 0;
    *out_height = 0;
    out_pixels->clear();

    const D2D1_SIZE_U pixel_size = source_bitmap->GetPixelSize();
    if (pixel_size.width == 0 || pixel_size.height == 0) {
        return E_FAIL;
    }

    const size_t row_bytes = static_cast<size_t>(pixel_size.width) * 4u;
    if (row_bytes / 4u != static_cast<size_t>(pixel_size.width)) {
        return E_FAIL;
    }
    if (row_bytes > std::numeric_limits<size_t>::max() / static_cast<size_t>(pixel_size.height)) {
        return E_FAIL;
    }
    const size_t pixel_bytes = row_bytes * static_cast<size_t>(pixel_size.height);

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> cpu_bitmap;
    HRESULT hr = d2d_context_->CreateBitmap(pixel_size, nullptr, 0, &props, &cpu_bitmap);
    if (FAILED(hr) || !cpu_bitmap) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = cpu_bitmap->CopyFromBitmap(nullptr, source_bitmap, nullptr);
    if (FAILED(hr)) {
        return hr;
    }

    D2D1_MAPPED_RECT mapped{};
    hr = cpu_bitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr) || !mapped.bits) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    out_pixels->resize(pixel_bytes);
    for (UINT32 y = 0; y < pixel_size.height; ++y) {
        memcpy(out_pixels->data() + static_cast<size_t>(y) * row_bytes,
               mapped.bits + static_cast<size_t>(y) * mapped.pitch,
               row_bytes);
    }
    cpu_bitmap->Unmap();

    *out_width = pixel_size.width;
    *out_height = pixel_size.height;
    return S_OK;
}

HRESULT QmiApp::ExtractCurrentImagePixels(UINT32* out_width,
                                          UINT32* out_height,
                                          std::vector<std::uint8_t>* out_pixels) {
    if (!out_width || !out_height || !out_pixels) {
        return E_POINTER;
    }
    *out_width = 0;
    *out_height = 0;
    out_pixels->clear();

    if (current_image_.type == ImageType::Raster && current_image_.raster) {
        return ReadBitmapPixels(current_image_.raster.Get(), out_width, out_height, out_pixels);
    }

    if (current_image_.type != ImageType::Svg || !current_image_.svg || !d2d_context_ || !d2d_context5_) {
        return E_FAIL;
    }

    const double width_d = std::max(1.0, static_cast<double>(current_image_.width));
    const double height_d = std::max(1.0, static_cast<double>(current_image_.height));
    if (width_d > static_cast<double>(std::numeric_limits<UINT32>::max()) ||
        height_d > static_cast<double>(std::numeric_limits<UINT32>::max())) {
        return E_FAIL;
    }

    const UINT32 target_width = std::max(1u, static_cast<UINT32>(std::lround(width_d)));
    const UINT32 target_height = std::max(1u, static_cast<UINT32>(std::lround(height_d)));
    const D2D1_BITMAP_PROPERTIES1 target_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> target_bitmap;
    HRESULT hr = d2d_context_->CreateBitmap(D2D1::SizeU(target_width, target_height), nullptr, 0, &target_props, &target_bitmap);
    if (FAILED(hr) || !target_bitmap) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    ComPtr<ID2D1Image> previous_target;
    d2d_context_->GetTarget(&previous_target);
    D2D1_MATRIX_3X2_F previous_transform = D2D1::Matrix3x2F::Identity();
    d2d_context_->GetTransform(&previous_transform);

    d2d_context_->SetTarget(target_bitmap.Get());
    d2d_context_->BeginDraw();
    d2d_context_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2d_context_->Clear(D2D1::ColorF(0, 0.0f));
    d2d_context5_->DrawSvgDocument(current_image_.svg.Get());
    hr = d2d_context_->EndDraw();

    d2d_context_->SetTransform(previous_transform);
    d2d_context_->SetTarget(previous_target.Get());

    if (FAILED(hr)) {
        return hr;
    }
    return ReadBitmapPixels(target_bitmap.Get(), out_width, out_height, out_pixels);
}

bool QmiApp::CopyCurrentImageToClipboard() {
    UINT32 width = 0;
    UINT32 height = 0;
    std::vector<std::uint8_t> pixels;
    if (FAILED(ExtractCurrentImagePixels(&width, &height, &pixels)) || width == 0 || height == 0) {
        return false;
    }

    const size_t row_bytes = static_cast<size_t>(width) * 4u;
    if (row_bytes / 4u != static_cast<size_t>(width)) {
        return false;
    }
    if (row_bytes > std::numeric_limits<size_t>::max() / static_cast<size_t>(height)) {
        return false;
    }
    const size_t pixel_bytes = row_bytes * static_cast<size_t>(height);
    if (pixels.size() < pixel_bytes) {
        return false;
    }
    if (width > static_cast<UINT32>(std::numeric_limits<LONG>::max()) ||
        height > static_cast<UINT32>(std::numeric_limits<LONG>::max()) ||
        pixel_bytes > static_cast<size_t>(std::numeric_limits<DWORD>::max())) {
        return false;
    }

    for (size_t i = 0; i + 3 < pixel_bytes; i += 4) {
        const std::uint8_t alpha = pixels[i + 3];
        pixels[i + 0] = UnpremultiplyChannel(pixels[i + 0], alpha);
        pixels[i + 1] = UnpremultiplyChannel(pixels[i + 1], alpha);
        pixels[i + 2] = UnpremultiplyChannel(pixels[i + 2], alpha);
    }

    const size_t total_bytes = sizeof(BITMAPINFOHEADER) + pixel_bytes;
    if (total_bytes < pixel_bytes) {
        return false;
    }

    HGLOBAL dib_handle = GlobalAlloc(GMEM_MOVEABLE, total_bytes);
    if (!dib_handle) {
        return false;
    }

    void* dib_memory = GlobalLock(dib_handle);
    if (!dib_memory) {
        GlobalFree(dib_handle);
        return false;
    }

    auto* header = static_cast<BITMAPINFOHEADER*>(dib_memory);
    ZeroMemory(header, sizeof(*header));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = static_cast<LONG>(width);
    header->biHeight = static_cast<LONG>(height);
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(pixel_bytes);

    auto* dib_pixels = reinterpret_cast<std::uint8_t*>(header + 1);
    for (UINT32 y = 0; y < height; ++y) {
        const size_t src_y = static_cast<size_t>(height - 1 - y);
        memcpy(dib_pixels + static_cast<size_t>(y) * row_bytes, pixels.data() + src_y * row_bytes, row_bytes);
    }
    GlobalUnlock(dib_handle);

    if (!OpenClipboard(hwnd_)) {
        GlobalFree(dib_handle);
        return false;
    }

    bool copied = false;
    if (EmptyClipboard() && SetClipboardData(CF_DIB, dib_handle)) {
        copied = true;
        dib_handle = nullptr;
    }

    CloseClipboard();
    if (dib_handle) {
        GlobalFree(dib_handle);
    }
    return copied;
}

bool QmiApp::CopyCurrentFileToClipboard() {
    if (current_image_.path.empty()) {
        return false;
    }

    std::error_code ec;
    fs::path absolute_path = fs::absolute(current_image_.path, ec);
    if (ec) {
        absolute_path = current_image_.path;
    }
    if (!fs::is_regular_file(absolute_path, ec) || ec) {
        return false;
    }

    const std::wstring file_path = absolute_path.lexically_normal().wstring();
    if (file_path.empty()) {
        return false;
    }

    if (file_path.size() > (std::numeric_limits<size_t>::max() / sizeof(wchar_t)) - 2) {
        return false;
    }
    const size_t path_bytes = (file_path.size() + 2) * sizeof(wchar_t);
    if (sizeof(DROPFILES) > std::numeric_limits<size_t>::max() - path_bytes) {
        return false;
    }
    const size_t drop_bytes = sizeof(DROPFILES) + path_bytes;

    HGLOBAL drop_handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, drop_bytes);
    if (!drop_handle) {
        return false;
    }

    void* drop_memory = GlobalLock(drop_handle);
    if (!drop_memory) {
        GlobalFree(drop_handle);
        return false;
    }

    auto* drop_files = static_cast<DROPFILES*>(drop_memory);
    drop_files->pFiles = sizeof(DROPFILES);
    drop_files->fWide = TRUE;

    auto* file_list =
        reinterpret_cast<wchar_t*>(reinterpret_cast<std::uint8_t*>(drop_memory) + sizeof(DROPFILES));
    memcpy(file_list, file_path.c_str(), file_path.size() * sizeof(wchar_t));
    file_list[file_path.size()] = L'\0';
    file_list[file_path.size() + 1] = L'\0';
    GlobalUnlock(drop_handle);

    HGLOBAL effect_handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    if (effect_handle) {
        void* effect_memory = GlobalLock(effect_handle);
        if (effect_memory) {
            *static_cast<DWORD*>(effect_memory) = DROPEFFECT_COPY;
            GlobalUnlock(effect_handle);
        } else {
            GlobalFree(effect_handle);
            effect_handle = nullptr;
        }
    }

    if (!OpenClipboard(hwnd_)) {
        GlobalFree(drop_handle);
        if (effect_handle) {
            GlobalFree(effect_handle);
        }
        return false;
    }

    bool copied = false;
    if (EmptyClipboard() && SetClipboardData(CF_HDROP, drop_handle)) {
        copied = true;
        drop_handle = nullptr;

        if (effect_handle) {
            const UINT preferred_drop_effect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (preferred_drop_effect != 0 && SetClipboardData(preferred_drop_effect, effect_handle)) {
                effect_handle = nullptr;
            }
        }
    }

    CloseClipboard();
    if (drop_handle) {
        GlobalFree(drop_handle);
    }
    if (effect_handle) {
        GlobalFree(effect_handle);
    }
    return copied;
}

bool QmiApp::CopyCurrentImagePathToClipboard() {
    if (current_image_.path.empty()) {
        return false;
    }

    std::error_code ec;
    fs::path absolute_path = fs::absolute(current_image_.path, ec);
    if (ec) {
        absolute_path = current_image_.path;
    }

    const std::wstring file_path = absolute_path.lexically_normal().wstring();
    if (file_path.empty()) {
        return false;
    }

    if (file_path.size() > (std::numeric_limits<size_t>::max() / sizeof(wchar_t)) - 1) {
        return false;
    }
    const size_t text_bytes = (file_path.size() + 1) * sizeof(wchar_t);
    HGLOBAL text_handle = GlobalAlloc(GMEM_MOVEABLE, text_bytes);
    if (!text_handle) {
        return false;
    }

    void* text_memory = GlobalLock(text_handle);
    if (!text_memory) {
        GlobalFree(text_handle);
        return false;
    }
    memcpy(text_memory, file_path.c_str(), text_bytes);
    GlobalUnlock(text_handle);

    if (!OpenClipboard(hwnd_)) {
        GlobalFree(text_handle);
        return false;
    }

    bool copied = false;
    if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, text_handle)) {
        copied = true;
        text_handle = nullptr;
    }

    CloseClipboard();
    if (text_handle) {
        GlobalFree(text_handle);
    }
    return copied;
}

bool QmiApp::OpenCurrentImageInFolder() {
    if (current_image_.path.empty()) {
        return false;
    }

    std::error_code ec;
    fs::path target_path = fs::absolute(current_image_.path, ec);
    if (ec) {
        target_path = current_image_.path;
    }
    if (target_path.empty()) {
        return false;
    }

    target_path = target_path.lexically_normal();
    std::wstring target = target_path.wstring();
    if (target.empty()) {
        return false;
    }

    PIDLIST_ABSOLUTE item_pidl = ILCreateFromPathW(target.c_str());
    if (item_pidl) {
        const HRESULT hr = SHOpenFolderAndSelectItems(item_pidl, 0, nullptr, 0);
        ILFree(item_pidl);
        if (SUCCEEDED(hr)) {
            return true;
        }
    }

    const fs::path parent = target_path.parent_path();
    if (parent.empty()) {
        return false;
    }
    const std::wstring parent_path = parent.lexically_normal().wstring();
    if (parent_path.empty()) {
        return false;
    }

    const HINSTANCE shell_result = ShellExecuteW(hwnd_, L"open", parent_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(shell_result) > 32;
}

bool QmiApp::PrintCurrentImage() {
    if (current_image_.path.empty()) {
        return false;
    }

    std::error_code ec;
    fs::path target_path = fs::absolute(current_image_.path, ec);
    if (ec) {
        target_path = current_image_.path;
    }
    if (!fs::is_regular_file(target_path, ec) || ec) {
        return false;
    }

    const std::wstring file_path = target_path.lexically_normal().wstring();
    if (file_path.empty()) {
        return false;
    }

    const HINSTANCE shell_result = ShellExecuteW(hwnd_, L"print", file_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(shell_result) > 32;
}

bool QmiApp::IsWindowTopMost() const {
    if (!hwnd_) {
        return false;
    }
    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    return (ex_style & WS_EX_TOPMOST) != 0;
}

bool QmiApp::SetWindowTopMost(bool topmost) {
    if (!hwnd_) {
        return false;
    }
    const HWND insert_after = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    return SetWindowPos(hwnd_, insert_after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
}

bool QmiApp::ToggleWindowTopMost() {
    return SetWindowTopMost(!IsWindowTopMost());
}

bool QmiApp::MoveCurrentFileToRecycleBin() {
    if (current_image_.path.empty()) {
        return false;
    }

    std::error_code path_ec;
    fs::path delete_path = fs::absolute(current_image_.path, path_ec);
    if (path_ec) {
        delete_path = current_image_.path;
    }
    if (delete_path.empty()) {
        return false;
    }

    fs::path next_hint;
    if (current_index_ >= 0 && current_index_ < static_cast<int>(images_.size()) && images_.size() > 1) {
        const int next_index = (current_index_ + 1 < static_cast<int>(images_.size())) ? (current_index_ + 1) : (current_index_ - 1);
        if (next_index >= 0 && next_index < static_cast<int>(images_.size())) {
            next_hint = images_[next_index];
        }
    }

    std::wstring delete_from = delete_path.lexically_normal().wstring();
    if (delete_from.empty()) {
        return false;
    }
    // SHFileOperation requires a double-null-terminated path list.
    delete_from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = delete_from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_SILENT | FOF_NOERRORUI;
    const int op_result = SHFileOperationW(&op);
    if (op_result != 0 || op.fAnyOperationsAborted) {
        return false;
    }

    if (deferred_directory_build_pending_) {
        deferred_directory_build_pending_ = false;
        deferred_directory_target_norm_.clear();
        if (hwnd_) {
            KillTimer(hwnd_, kStartupScanTimerId);
        }
    }

    fs::path next_path;
    if (!next_hint.empty()) {
        std::error_code next_ec;
        if (fs::is_regular_file(next_hint, next_ec) && !next_ec) {
            next_path = next_hint;
        }
    }
    if (next_path.empty()) {
        const fs::path directory = delete_path.parent_path();
        if (!directory.empty()) {
            const std::optional<fs::path> first_supported = FindFirstSupportedImageInDirectory(directory);
            if (first_supported) {
                next_path = *first_supported;
            }
        }
    }

    if (!next_path.empty() && OpenImagePath(next_path, true)) {
        return true;
    }

    ClearAnimationState();
    ClearDoubleClickZoomRestore();
    current_image_ = LoadedImage{};
    current_error_.clear();
    ClearCurrentImageInfo();
    images_.clear();
    thumbnails_.clear();
    thumbnail_draw_scales_.clear();
    visible_thumbs_.clear();
    current_index_ = -1;
    film_strip_scroll_index_ = -1;
    hover_open_button_ = false;
    pressed_open_button_ = false;
    hover_edge_nav_button_ = EdgeNavButton::None;
    visible_edge_nav_button_ = EdgeNavButton::None;
    pressed_edge_nav_button_ = EdgeNavButton::None;
    hover_thumbnail_index_ = -1;
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    ResetImageTransform();

    if (hwnd_) {
        SetWindowTextW(hwnd_, L"Qmi");
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return true;
}

void QmiApp::ShowContextMenu(POINT screen_pt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    const bool can_copy_image = IsRenderableImageTypeForActions(current_image_.type);
    const bool can_copy_file = !current_image_.path.empty();
    const bool can_copy_path = can_copy_file;
    const bool can_delete_file = can_copy_file;
    const bool can_open_containing_folder = can_copy_file;
    const bool can_print_image = can_copy_file;
    const bool can_transform_image = IsRenderableImageTypeForActions(current_image_.type);
    const bool is_top_most = IsWindowTopMost();
    AppendMenuW(menu, MF_STRING, kMenuOpenFile, L"\u6253\u5f00\u56fe\u7247");
    AppendMenuW(menu,
                can_open_containing_folder ? MF_STRING : (MF_STRING | MF_GRAYED),
                kMenuOpenContainingFolder,
                L"\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939");
    AppendMenuW(menu, can_print_image ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuPrintImage, L"\u6253\u5370\u56fe\u7247");
    AppendMenuW(menu, can_copy_image ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuCopyImage, L"\u590d\u5236\u56fe\u7247");
    AppendMenuW(menu, can_copy_file ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuCopyFile, L"\u590d\u5236\u6587\u4ef6");
    AppendMenuW(menu, can_copy_path ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuCopyImagePath, L"\u590d\u5236\u56fe\u7247\u8def\u5f84");
    AppendMenuW(menu, can_delete_file ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuDeleteFile, L"\u5220\u9664\u6587\u4ef6");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,
                can_transform_image ? MF_STRING : (MF_STRING | MF_GRAYED),
                kMenuRotateClockwise,
                L"\u987a\u65f6\u9488\u65cb\u8f6c 90\u5ea6");
    AppendMenuW(menu,
                can_transform_image ? MF_STRING : (MF_STRING | MF_GRAYED),
                kMenuRotateCounterclockwise,
                L"\u9006\u65f6\u9488\u65cb\u8f6c 90\u5ea6");
    AppendMenuW(menu,
                can_transform_image ? MF_STRING : (MF_STRING | MF_GRAYED),
                kMenuFlipHorizontal,
                L"\u6c34\u5e73\u955c\u50cf\u7ffb\u8f6c");
    AppendMenuW(menu,
                can_transform_image ? MF_STRING : (MF_STRING | MF_GRAYED),
                kMenuFlipVertical,
                L"\u5782\u76f4\u955c\u50cf\u7ffb\u8f6c");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (is_top_most ? MF_CHECKED : 0), kMenuToggleTopMost, L"\u7a97\u53e3\u7f6e\u9876");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"\u7a0b\u5e8f\u8bbe\u7f6e");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"\u9000\u51fa\u7a0b\u5e8f");

    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuOpenFile:
            OpenFileDialog();
            break;
        case kMenuOpenContainingFolder:
            if (!OpenCurrentImageInFolder()) {
                MessageBoxW(hwnd_,
                            L"\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939\u5931\u8d25\uff0c\u6587\u4ef6\u53ef\u80fd\u4e0d\u5b58\u5728\u6216\u8def\u5f84\u4e0d\u53ef\u8bbf\u95ee\u3002",
                            L"Qmi",
                            MB_ICONERROR | MB_OK);
            }
            break;
        case kMenuPrintImage:
            if (!PrintCurrentImage()) {
                MessageBoxW(hwnd_,
                            L"\u6253\u5370\u56fe\u7247\u5931\u8d25\uff0c\u8bf7\u786e\u8ba4\u7cfb\u7edf\u5df2\u914d\u7f6e\u6253\u5370\u673a\uff0c\u5e76\u5177\u6709\u53ef\u7528\u7684\u9ed8\u8ba4\u6253\u5370\u7a0b\u5e8f\u3002",
                            L"Qmi",
                            MB_ICONERROR | MB_OK);
            }
            break;
        case kMenuCopyImage:
            CopyCurrentImageToClipboard();
            break;
        case kMenuCopyFile:
            CopyCurrentFileToClipboard();
            break;
        case kMenuCopyImagePath:
            CopyCurrentImagePathToClipboard();
            break;
        case kMenuDeleteFile:
            if (!MoveCurrentFileToRecycleBin()) {
                MessageBoxW(hwnd_,
                            L"\u5220\u9664\u6587\u4ef6\u5931\u8d25\uff0c\u6587\u4ef6\u53ef\u80fd\u4e0d\u5b58\u5728\u6216\u65e0\u6cd5\u79fb\u5165\u56de\u6536\u7ad9\u3002",
                            L"Qmi",
                            MB_ICONERROR | MB_OK);
            }
            break;
        case kMenuRotateClockwise:
            RotateImageClockwise();
            break;
        case kMenuRotateCounterclockwise:
            RotateImageCounterclockwise();
            break;
        case kMenuFlipHorizontal:
            ToggleImageFlipHorizontal();
            break;
        case kMenuFlipVertical:
            ToggleImageFlipVertical();
            break;
        case kMenuToggleTopMost:
            if (!ToggleWindowTopMost()) {
                MessageBoxW(hwnd_, L"\u8bbe\u7f6e\u7a97\u53e3\u7f6e\u9876\u5931\u8d25\u3002", L"Qmi", MB_ICONERROR | MB_OK);
            }
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
