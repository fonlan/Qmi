#include "qmi_app.h"

#include "qmi_utils.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr UINT_PTR kStartupScanTimerId = 2;

bool IsRenderableImageTypeForInfo(ImageType type) {
    return type == ImageType::Raster || type == ImageType::Svg;
}
}  // namespace

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
    const std::wstring target_path_key = NormalizePathLower(target);

    struct DirectoryImageEntry {
        fs::path path;
        std::wstring path_key;
        std::wstring name_key;
        std::wstring extension_key;
        ULONGLONG file_size = 0;
        ULONGLONG creation_time = 0;
        ULONGLONG modified_time = 0;
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

            WIN32_FILE_ATTRIBUTE_DATA file_data{};
            ULONGLONG file_size = 0;
            ULONGLONG creation_time = 0;
            ULONGLONG modified_time = 0;
            if (GetFileAttributesExW(entry_path.c_str(), GetFileExInfoStandard, &file_data)) {
                file_size =
                    (static_cast<ULONGLONG>(file_data.nFileSizeHigh) << 32u) | static_cast<ULONGLONG>(file_data.nFileSizeLow);
                creation_time = (static_cast<ULONGLONG>(file_data.ftCreationTime.dwHighDateTime) << 32u) |
                                static_cast<ULONGLONG>(file_data.ftCreationTime.dwLowDateTime);
                modified_time = (static_cast<ULONGLONG>(file_data.ftLastWriteTime.dwHighDateTime) << 32u) |
                                static_cast<ULONGLONG>(file_data.ftLastWriteTime.dwLowDateTime);
            }

            DirectoryImageEntry entry{};
            entry.path = entry_path;
            entry.path_key = NormalizePathLower(entry_path);
            entry.name_key = ToLower(entry_path.filename().wstring());
            entry.extension_key = ToLower(entry_path.extension().wstring());
            entry.file_size = file_size;
            entry.creation_time = creation_time;
            entry.modified_time = modified_time;
            entry.is_target = entry.path_key == target_path_key;
            found.push_back(std::move(entry));
        }
    }

    if (found.empty()) {
        DirectoryImageEntry entry{};
        entry.path = target;
        entry.path_key = target_path_key;
        entry.name_key = ToLower(target.filename().wstring());
        entry.extension_key = ToLower(target.extension().wstring());
        entry.is_target = true;
        found.push_back(std::move(entry));
    }

    const int sort_field = ClampFilmStripSortField(film_strip_sort_field_);
    const bool sort_descending = film_strip_sort_descending_;
    std::sort(found.begin(), found.end(), [sort_field, sort_descending](const DirectoryImageEntry& a, const DirectoryImageEntry& b) {
        auto compare_string = [](const std::wstring& lhs, const std::wstring& rhs) -> int {
            if (lhs < rhs) {
                return -1;
            }
            if (lhs > rhs) {
                return 1;
            }
            return 0;
        };
        auto compare_u64 = [](ULONGLONG lhs, ULONGLONG rhs) -> int {
            if (lhs < rhs) {
                return -1;
            }
            if (lhs > rhs) {
                return 1;
            }
            return 0;
        };

        int cmp = 0;
        switch (sort_field) {
            case kFilmStripSortFieldFileSize:
                cmp = compare_u64(a.file_size, b.file_size);
                break;
            case kFilmStripSortFieldFileExtension:
                cmp = compare_string(a.extension_key, b.extension_key);
                break;
            case kFilmStripSortFieldCreationTime:
                cmp = compare_u64(a.creation_time, b.creation_time);
                break;
            case kFilmStripSortFieldModifiedTime:
                cmp = compare_u64(a.modified_time, b.modified_time);
                break;
            case kFilmStripSortFieldFileName:
            default:
                cmp = compare_string(a.name_key, b.name_key);
                break;
        }

        if (cmp == 0) {
            cmp = compare_string(a.name_key, b.name_key);
        }
        if (cmp == 0) {
            cmp = compare_string(a.extension_key, b.extension_key);
        }
        if (cmp == 0) {
            cmp = compare_u64(a.file_size, b.file_size);
        }
        if (cmp == 0) {
            cmp = compare_u64(a.creation_time, b.creation_time);
        }
        if (cmp == 0) {
            cmp = compare_u64(a.modified_time, b.modified_time);
        }
        if (cmp == 0) {
            cmp = compare_string(a.path_key, b.path_key);
        }
        if (cmp == 0) {
            return false;
        }
        if (sort_descending) {
            cmp = -cmp;
        }
        return cmp < 0;
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
    thumbnail_draw_scales_.assign(images_.size(), 0.0f);
    film_strip_scale_anim_tick_ = 0;
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

void QmiApp::ClearCurrentImageInfo() {
    current_image_info_.clear();
}

void QmiApp::UpdateCurrentImageInfo() {
    if (current_image_.type == ImageType::None || current_image_.path.empty()) {
        ClearCurrentImageInfo();
        return;
    }

    std::wstring file_name = current_image_.path.filename().wstring();
    if (file_name.empty()) {
        file_name = current_image_.path.wstring();
    }

    std::wstring resolution = L"-";
    if (IsRenderableImageTypeForInfo(current_image_.type)) {
        const int pixel_w = std::max(1, static_cast<int>(std::lround(current_image_.width)));
        const int pixel_h = std::max(1, static_cast<int>(std::lround(current_image_.height)));
        resolution = std::to_wstring(pixel_w) + L"x" + std::to_wstring(pixel_h);
    } else if (current_image_.type == ImageType::Broken) {
        resolution = L"\u635f\u574f/\u65e0\u6cd5\u89e3\u7801";
    }

    std::wstring file_size = L"-";
    std::wstring modified_time = L"-";
    WIN32_FILE_ATTRIBUTE_DATA file_data{};
    if (GetFileAttributesExW(current_image_.path.c_str(), GetFileExInfoStandard, &file_data)) {
        const ULONGLONG bytes =
            (static_cast<ULONGLONG>(file_data.nFileSizeHigh) << 32u) | static_cast<ULONGLONG>(file_data.nFileSizeLow);
        file_size = FormatFileSizeText(bytes);
        modified_time = FormatFileTimeText(file_data.ftLastWriteTime);
    }

    current_image_info_ = file_name + L" | " + resolution + L" | " + file_size + L" | " + modified_time;
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

    const bool decode_failed = FAILED(hr) || image.type == ImageType::None;
    if (decode_failed) {
        image.type = ImageType::Broken;
        image.width = 0.0f;
        image.height = 0.0f;
        // Broken images are represented directly in the viewport placeholder.
        // Do not also show a bottom global error overlay behind the filmstrip.
        current_error_.clear();
    } else {
        current_error_.clear();
    }

    current_image_ = std::move(image);
    ClearDoubleClickZoomRestore();
    if (index != current_index_) {
        film_strip_scroll_index_ = -1;
    }
    current_index_ = index;
    UpdateCurrentImageInfo();
    ResetImageTransform();
    hover_open_button_ = false;
    pressed_open_button_ = false;
    hover_edge_nav_button_ = EdgeNavButton::None;
    visible_edge_nav_button_ = EdgeNavButton::None;
    pressed_edge_nav_button_ = EdgeNavButton::None;
    hover_thumbnail_index_ = -1;

    if (reset_view && fit_on_switch_) {
        ResetView();
    }

    if (animation_decoder_ && animation_frame_delays_ms_.size() > 1) {
        ScheduleNextAnimationFrame();
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool QmiApp::OpenImagePath(const fs::path& path, bool reset_view, bool defer_directory_scan) {
    if (path.empty() || !IsSupportedExtension(path)) {
        current_error_ = L"\u4e0d\u652f\u6301\u7684\u6587\u4ef6\u683c\u5f0f\u3002";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (defer_directory_scan) {
        images_.clear();
        images_.push_back(path);
        current_index_ = 0;
        thumbnails_.assign(1, Thumbnail{});
        thumbnail_draw_scales_.assign(1, 0.0f);
        film_strip_scale_anim_tick_ = 0;
        film_strip_scroll_index_ = -1;
    } else {
        BuildDirectoryList(path);
    }

    if (images_.empty()) {
        current_error_ = L"\u6b64\u6587\u4ef6\u5939\u4e2d\u6ca1\u6709\u652f\u6301\u7684\u56fe\u7247\u3002";
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

    const int count = static_cast<int>(images_.size());
    if (count <= 0) {
        return;
    }

    int normalized = delta % count;
    if (normalized < 0) {
        normalized += count;
    }
    const int next = (current_index_ + normalized) % count;
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
