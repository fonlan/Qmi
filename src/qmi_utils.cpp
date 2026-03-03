#include "qmi_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace fs = std::filesystem;

namespace {
constexpr int kAppIconResourceId = 101;

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
}  // namespace

std::uint8_t UnpremultiplyChannel(std::uint8_t value, std::uint8_t alpha) {
    if (alpha == 0) {
        return 0;
    }
    if (alpha == 255) {
        return value;
    }
    const unsigned result = (static_cast<unsigned>(value) * 255u + alpha / 2u) / alpha;
    return static_cast<std::uint8_t>(std::min(255u, result));
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

std::wstring FormatFileSizeText(ULONGLONG bytes) {
    constexpr std::array<const wchar_t*, 5> kUnits = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit_index;
    }

    wchar_t buffer[64] = {};
    if (unit_index == 0) {
        swprintf_s(buffer, L"%llu %ls", static_cast<unsigned long long>(bytes), kUnits[unit_index]);
    } else if (value >= 100.0) {
        swprintf_s(buffer, L"%.0f %ls", value, kUnits[unit_index]);
    } else if (value >= 10.0) {
        swprintf_s(buffer, L"%.1f %ls", value, kUnits[unit_index]);
    } else {
        swprintf_s(buffer, L"%.2f %ls", value, kUnits[unit_index]);
    }
    return buffer;
}

std::wstring FormatFileTimeText(const FILETIME& utc_filetime) {
    FILETIME local_filetime{};
    SYSTEMTIME local_time{};
    if (!FileTimeToLocalFileTime(&utc_filetime, &local_filetime) ||
        !FileTimeToSystemTime(&local_filetime, &local_time)) {
        return L"-";
    }

    wchar_t buffer[64] = {};
    swprintf_s(buffer,
               L"%04u-%02u-%02u %02u:%02u",
               local_time.wYear,
               local_time.wMonth,
               local_time.wDay,
               local_time.wHour,
               local_time.wMinute);
    return buffer;
}

std::wstring FormatZoomPercentText(float scale) {
    const float percent = std::max(0.0f, scale * 100.0f);
    const float rounded_percent = std::round(percent);

    wchar_t buffer[32] = {};
    if (std::fabs(percent - rounded_percent) <= 0.05f) {
        swprintf_s(buffer, L"\u7f29\u653e %.0f%%", rounded_percent);
    } else {
        swprintf_s(buffer, L"\u7f29\u653e %.1f%%", percent);
    }
    return buffer;
}

bool IsSupportedExtension(const fs::path& p) {
    const std::wstring ext = ToLower(p.extension().wstring());
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" || ext == L".bmp" || ext == L".webp" ||
           ext == L".gif" || ext == L".heic" || ext == L".heif" || ext == L".svg" || ext == L".ico";
}

bool IsWebpExtension(const fs::path& p) {
    return ToLower(p.extension().wstring()) == L".webp";
}

bool IsGifExtension(const fs::path& p) {
    return ToLower(p.extension().wstring()) == L".gif";
}

bool IsIcoExtension(const fs::path& p) {
    return ToLower(p.extension().wstring()) == L".ico";
}

HICON LoadAppIcon(HINSTANCE hinstance, int width, int height) {
    return reinterpret_cast<HICON>(LoadImageW(
        hinstance,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR | LR_SHARED));
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

