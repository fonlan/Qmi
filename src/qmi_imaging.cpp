#include "qmi_app.h"

#include "qmi_utils.h"

#include <shlwapi.h>
#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr UINT_PTR kAnimationTimerId = 3;
constexpr UINT kGifDefaultDelayMs = 100;
constexpr UINT kGifMinDelayMs = 16;

bool IsAsciiWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string TrimAscii(std::string value) {
    std::size_t first = 0;
    while (first < value.size() && IsAsciiWhitespace(value[first])) {
        ++first;
    }
    if (first == value.size()) {
        return {};
    }
    std::size_t last = value.size();
    while (last > first && IsAsciiWhitespace(value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}
std::string NormalizeSvgColorToken(std::string value) {
    value = TrimAscii(value);
    int red = 0;
    int green = 0;
    int blue = 0;
    if (std::sscanf(value.c_str(), "rgb(%d, %d, %d)", &red, &green, &blue) == 3) {
        red = std::clamp(red, 0, 255);
        green = std::clamp(green, 0, 255);
        blue = std::clamp(blue, 0, 255);
        char hex[8]{};
        std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", red, green, blue);
        return hex;
    }
    return value;
}

std::string NormalizeSvgLightDarkColors(const std::string& svg) {
    std::string normalized;
    normalized.reserve(svg.size());
    std::size_t pos = 0;
    constexpr std::string_view marker = "light-dark(";
    while (pos < svg.size()) {
        const std::size_t start = svg.find(marker, pos);
        if (start == std::string::npos) {
            normalized.append(svg, pos, std::string::npos);
            break;
        }
        normalized.append(svg, pos, start - pos);
        std::size_t scan = start + marker.size();
        int depth = 1;
        std::size_t comma = std::string::npos;
        for (; scan < svg.size(); ++scan) {
            const char ch = svg[scan];
            if (ch == '(') {
                ++depth;
            } else if (ch == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            } else if (ch == ',' && depth == 1 && comma == std::string::npos) {
                comma = scan;
            }
        }
        if (scan >= svg.size() || comma == std::string::npos) {
            normalized.append(svg, start, std::string::npos);
            break;
        }
        const std::size_t first_start = start + marker.size();
        normalized.append(NormalizeSvgColorToken(svg.substr(first_start, comma - first_start)));
        pos = scan + 1;
    }
    return normalized;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::wstring DecodeSvgText(std::string value) {
    std::wstring decoded;
    for (std::size_t pos = 0; pos < value.size();) {
        if (value[pos] == '&') {
            const std::size_t end = value.find(';', pos + 1);
            if (end != std::string::npos) {
                const std::string entity = value.substr(pos + 1, end - pos - 1);
                if (entity == "amp") {
                    decoded.push_back(L'&');
                    pos = end + 1;
                    continue;
                }
                if (entity == "lt") {
                    decoded.push_back(L'<');
                    pos = end + 1;
                    continue;
                }
                if (entity == "gt") {
                    decoded.push_back(L'>');
                    pos = end + 1;
                    continue;
                }
                if (entity == "quot") {
                    decoded.push_back(L'\"');
                    pos = end + 1;
                    continue;
                }
                if (entity == "apos") {
                    decoded.push_back(L'\'');
                    pos = end + 1;
                    continue;
                }
            }
        }
        const std::size_t next = value.find('&', pos + 1);
        decoded += Utf8ToWide(value.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
        if (next == std::string::npos) {
            break;
        }
        pos = next;
    }
    return decoded;
}

std::string GetSvgAttribute(std::string_view tag, std::string_view name) {
    std::string pattern(name);
    pattern += "=\"";
    std::size_t start = tag.find(pattern);
    if (start == std::string_view::npos) {
        return {};
    }
    start += pattern.size();
    const std::size_t end = tag.find('"', start);
    if (end == std::string_view::npos) {
        return {};
    }
    return std::string(tag.substr(start, end - start));
}

float ParseSvgFloat(const std::string& value, float fallback) {
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    return end == value.c_str() ? fallback : parsed;
}

bool TextLooksTruncated(const std::wstring& value) {
    return value.size() >= 3 && value.substr(value.size() - 3) == L"...";
}

D2D1_COLOR_F ParseSvgColor(const std::string& value) {
    if (value.size() == 7 && value[0] == '#') {
        const unsigned int rgb = static_cast<unsigned int>(std::strtoul(value.c_str() + 1, nullptr, 16));
        return D2D1::ColorF(rgb, 1.0f);
    }
    return D2D1::ColorF(0x000000, 1.0f);
}

std::string StripSvgTags(std::string value) {
    for (std::size_t pos = 0; (pos = value.find("<br", pos)) != std::string::npos;) {
        const std::size_t end = value.find('>', pos);
        if (end == std::string::npos) {
            break;
        }
        value.replace(pos, end - pos + 1, "\n");
        ++pos;
    }
    std::string stripped;
    stripped.reserve(value.size());
    bool in_tag = false;
    for (char ch : value) {
        if (ch == '<') {
            in_tag = true;
        } else if (ch == '>') {
            in_tag = false;
        } else if (!in_tag) {
            stripped.push_back(ch);
        }
    }
    return stripped;
}

std::string GetCssPixelValue(std::string_view style, std::string_view name) {
    std::size_t start = style.find(name);
    if (start == std::string_view::npos) {
        return {};
    }
    start += name.size();
    while (start < style.size() && IsAsciiWhitespace(style[start])) {
        ++start;
    }
    if (start >= style.size() || style[start] != ':') {
        return {};
    }
    ++start;
    while (start < style.size() && IsAsciiWhitespace(style[start])) {
        ++start;
    }
    const std::size_t end = style.find_first_of(";p", start);
    if (end == std::string_view::npos) {
        return std::string(style.substr(start));
    }
    return std::string(style.substr(start, end - start));
}

void ParseSvgForeignObjectRuns(const std::string& svg, std::vector<SvgTextRun>* out_texts) {
    std::size_t pos = 0;
    while (true) {
        const std::size_t start = svg.find("<foreignObject", pos);
        if (start == std::string::npos) {
            break;
        }
        const std::size_t end = svg.find("</foreignObject>", start);
        if (end == std::string::npos) {
            break;
        }
        const std::size_t fallback_start = svg.find("<text", end);
        const std::size_t fallback_tag_end = fallback_start == std::string::npos ? std::string::npos : svg.find('>', fallback_start);
        const std::size_t fallback_close = fallback_tag_end == std::string::npos ? std::string::npos : svg.find("</text>", fallback_tag_end);
        if (fallback_start == std::string::npos || fallback_tag_end == std::string::npos || fallback_close == std::string::npos) {
            pos = end + 16;
            continue;
        }
        const std::string block = svg.substr(start, end - start);
        const std::string_view fallback_tag(svg.data() + fallback_start, fallback_tag_end - fallback_start + 1);
        const std::wstring fallback_text = DecodeSvgText(svg.substr(fallback_tag_end + 1, fallback_close - fallback_tag_end - 1));
        const std::size_t outer_style_start = block.find("<div ");
        if (outer_style_start == std::string::npos) {
            pos = fallback_close + 7;
            continue;
        }
        const std::size_t outer_style_end = block.find('>', outer_style_start);
        const std::string_view outer_tag(block.data() + outer_style_start, outer_style_end - outer_style_start + 1);
        const std::string outer_style = GetSvgAttribute(outer_tag, "style");
        const float width = ParseSvgFloat(GetCssPixelValue(outer_style, "width"), 0.0f);
        const float height = ParseSvgFloat(GetCssPixelValue(outer_style, "height"), 0.0f);
        const std::size_t inner_start = block.rfind("<div ");
        if (inner_start == std::string::npos || inner_start == outer_style_start) {
            pos = fallback_close + 7;
            continue;
        }
        const std::size_t inner_tag_end = block.find('>', inner_start);
        const std::size_t inner_close = block.find("</div>", inner_tag_end);
        if (inner_tag_end == std::string::npos || inner_close == std::string::npos) {
            pos = fallback_close + 7;
            continue;
        }
        const std::string_view inner_tag(block.data() + inner_start, inner_tag_end - inner_start + 1);
        const std::string inner_style = GetSvgAttribute(inner_tag, "style");
        SvgTextRun run;
        run.text = DecodeSvgText(StripSvgTags(block.substr(inner_tag_end + 1, inner_close - inner_tag_end - 1)));
        if (run.text.empty() || (TextLooksTruncated(fallback_text) && width > 2000.0f)) {
            pos = fallback_close + 7;
            continue;
        }
        run.x = ParseSvgFloat(GetSvgAttribute(fallback_tag, "x"), 0.0f);
        run.y = ParseSvgFloat(GetSvgAttribute(fallback_tag, "y"), 0.0f);
        run.font_size = ParseSvgFloat(GetSvgAttribute(fallback_tag, "font-size"), ParseSvgFloat(GetCssPixelValue(inner_style, "font-size"), 12.0f));
        const std::string fallback_fill = GetSvgAttribute(fallback_tag, "fill");
        run.fill = ParseSvgColor(fallback_fill.empty() ? NormalizeSvgColorToken(GetCssPixelValue(inner_style, "color")) : fallback_fill);
        const std::string anchor = GetSvgAttribute(fallback_tag, "text-anchor");
        run.text_anchor = anchor == "middle" ? 1 : (anchor == "end" ? 2 : 0);
        const std::string weight = GetSvgAttribute(fallback_tag, "font-weight");
        run.bold = weight == "bold" || weight == "700" || inner_style.find("font-weight: bold") != std::string::npos ||
                   block.find("<b>", inner_tag_end) != std::string::npos || block.find("<strong>", inner_tag_end) != std::string::npos;
        const std::size_t line_count = static_cast<std::size_t>(std::count(run.text.begin(), run.text.end(), L'\n')) + 1;
        run.width = width > 10.0f ? width : 0.0f;
        run.height = std::max(height, static_cast<float>(line_count) * run.font_size * 1.35f);
        run.box = width > 10.0f;
        out_texts->push_back(std::move(run));
        pos = fallback_close + 7;
    }
}

void ParseSvgFallbackTextRuns(const std::string& svg, std::vector<SvgTextRun>* out_texts) {
    std::size_t pos = 0;
    while (true) {
        const std::size_t tag_start = svg.find("<text", pos);
        if (tag_start == std::string::npos) {
            break;
        }
        const std::size_t tag_end = svg.find('>', tag_start);
        const std::size_t close = svg.find("</text>", tag_end == std::string::npos ? tag_start : tag_end);
        if (tag_end == std::string::npos || close == std::string::npos) {
            break;
        }
        const std::string_view tag(svg.data() + tag_start, tag_end - tag_start + 1);
        const std::string raw_text = svg.substr(tag_end + 1, close - tag_end - 1);
        SvgTextRun run;
        run.text = DecodeSvgText(raw_text);
        if (run.text.empty() || run.text == L"Text is not SVG - cannot display") {
            pos = close + 7;
            continue;
        }
        run.x = ParseSvgFloat(GetSvgAttribute(tag, "x"), 0.0f);
        run.y = ParseSvgFloat(GetSvgAttribute(tag, "y"), 0.0f);
        run.font_size = ParseSvgFloat(GetSvgAttribute(tag, "font-size"), 12.0f);
        run.fill = ParseSvgColor(GetSvgAttribute(tag, "fill"));
        const std::string anchor = GetSvgAttribute(tag, "text-anchor");
        run.text_anchor = anchor == "middle" ? 1 : (anchor == "end" ? 2 : 0);
        const std::string weight = GetSvgAttribute(tag, "font-weight");
        run.bold = weight == "bold" || weight == "700";
        out_texts->push_back(std::move(run));
        pos = close + 7;
    }
}

void ParseSvgTextRuns(const std::string& svg, std::vector<SvgTextRun>* out_texts) {
    if (!out_texts) {
        return;
    }
    out_texts->clear();
    ParseSvgForeignObjectRuns(svg, out_texts);
    if (out_texts->empty()) {
        ParseSvgFallbackTextRuns(svg, out_texts);
    }
}

HRESULT CreateStreamFromBytes(const std::string& bytes, IStream** out_stream) {
    if (!out_stream) {
        return E_POINTER;
    }
    *out_stream = nullptr;
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!global) {
        return E_OUTOFMEMORY;
    }
    void* data = GlobalLock(global);
    if (!data) {
        GlobalFree(global);
        return E_OUTOFMEMORY;
    }
    memcpy(data, bytes.data(), bytes.size());
    GlobalUnlock(global);
    HRESULT hr = CreateStreamOnHGlobal(global, TRUE, out_stream);
    if (FAILED(hr)) {
        GlobalFree(global);
    }
    return hr;
}
}  // namespace

void QmiApp::ClearAnimationState() {
    if (hwnd_) {
        KillTimer(hwnd_, kAnimationTimerId);
    }
    animation_decoder_.Reset();
    animation_frame_descriptors_.clear();
    animation_frame_delays_ms_.clear();
    animation_frame_index_ = 0;
    animation_canvas_.clear();
    animation_restore_canvas_.clear();
    animation_has_restore_canvas_ = false;
    animation_canvas_width_ = 0;
    animation_canvas_height_ = 0;
    animation_canvas_stride_ = 0;
    animation_prev_disposal_ = 0;
    animation_prev_left_ = 0;
    animation_prev_top_ = 0;
    animation_prev_width_ = 0;
    animation_prev_height_ = 0;
}

void QmiApp::ScheduleNextAnimationFrame() {
    if (!hwnd_ || !animation_decoder_ || animation_frame_delays_ms_.size() <= 1 ||
        animation_frame_index_ >= animation_frame_delays_ms_.size()) {
        return;
    }
    const UINT delay = std::max<UINT>(1u, animation_frame_delays_ms_[animation_frame_index_]);
    SetTimer(hwnd_, kAnimationTimerId, delay, nullptr);
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

    UINT frame_index = 0;
    if (IsIcoExtension(path)) {
        UINT frame_count = 0;
        if (SUCCEEDED(decoder->GetFrameCount(&frame_count)) && frame_count > 1) {
            std::uint64_t best_area = 0;
            UINT best_index = 0;
            for (UINT i = 0; i < frame_count; ++i) {
                ComPtr<IWICBitmapFrameDecode> candidate;
                if (FAILED(decoder->GetFrame(i, &candidate)) || !candidate) {
                    continue;
                }
                UINT candidate_w = 0;
                UINT candidate_h = 0;
                if (FAILED(candidate->GetSize(&candidate_w, &candidate_h)) || candidate_w == 0 || candidate_h == 0) {
                    continue;
                }
                const std::uint64_t area = static_cast<std::uint64_t>(candidate_w) * static_cast<std::uint64_t>(candidate_h);
                if (area > best_area) {
                    best_area = area;
                    best_index = i;
                }
            }
            frame_index = best_index;
        }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(frame_index, &frame);
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

    ClearAnimationState();

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

    animation_decoder_ = decoder;
    animation_frame_descriptors_.reserve(frame_count);
    animation_frame_delays_ms_.reserve(frame_count);
    animation_canvas_ = std::vector<std::uint8_t>(canvas_size, 0);
    animation_canvas_width_ = canvas_width;
    animation_canvas_height_ = canvas_height;
    animation_canvas_stride_ = static_cast<UINT>(canvas_stride);

    for (UINT i = 0; i < frame_count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = animation_decoder_->GetFrame(i, &frame);
        if (FAILED(hr) || !frame) {
            ClearAnimationState();
            return FAILED(hr) ? hr : E_FAIL;
        }

        UINT decode_width = 0;
        UINT decode_height = 0;
        hr = frame->GetSize(&decode_width, &decode_height);
        if (FAILED(hr) || decode_width == 0 || decode_height == 0) {
            ClearAnimationState();
            return FAILED(hr) ? hr : E_FAIL;
        }

        GifFrameDescriptor frame_desc{};
        frame_desc.width = decode_width;
        frame_desc.height = decode_height;
        UINT frame_delay_cs = 0;

        ComPtr<IWICMetadataQueryReader> frame_reader;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&frame_reader)) && frame_reader) {
            TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Left", &frame_desc.left);
            TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Top", &frame_desc.top);
            UINT metadata_width = 0;
            UINT metadata_height = 0;
            if (TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Width", &metadata_width) && metadata_width > 0) {
                frame_desc.width = metadata_width;
            }
            if (TryReadMetadataUInt(frame_reader.Get(), L"/imgdesc/Height", &metadata_height) && metadata_height > 0) {
                frame_desc.height = metadata_height;
            }
            TryReadMetadataUInt(frame_reader.Get(), L"/grctlext/Disposal", &frame_desc.disposal);
            TryReadMetadataUInt(frame_reader.Get(), L"/grctlext/Delay", &frame_delay_cs);
        }

        ULONGLONG delay_ms = frame_delay_cs > 0 ? static_cast<ULONGLONG>(frame_delay_cs) * 10ull : kGifDefaultDelayMs;
        delay_ms = std::max<ULONGLONG>(delay_ms, kGifMinDelayMs);
        if (delay_ms > std::numeric_limits<UINT>::max()) {
            delay_ms = std::numeric_limits<UINT>::max();
        }

        animation_frame_descriptors_.push_back(frame_desc);
        animation_frame_delays_ms_.push_back(static_cast<UINT>(delay_ms));
    }

    if (animation_frame_descriptors_.empty() || animation_frame_delays_ms_.size() != animation_frame_descriptors_.size()) {
        ClearAnimationState();
        return E_FAIL;
    }

    ComPtr<ID2D1Bitmap1> first_frame;
    hr = DecodeGifFrame(0, &first_frame);
    if (FAILED(hr) || !first_frame) {
        ClearAnimationState();
        return FAILED(hr) ? hr : E_FAIL;
    }

    out_image->type = ImageType::Raster;
    out_image->raster = first_frame;
    out_image->width = static_cast<float>(canvas_width);
    out_image->height = static_cast<float>(canvas_height);
    animation_frame_index_ = 0;
    return S_OK;
}

HRESULT QmiApp::DecodeGifFrame(size_t frame_index, ID2D1Bitmap1** out_bitmap) {
    if (!out_bitmap || !d2d_context_ || !wic_factory_ || !animation_decoder_ || frame_index >= animation_frame_descriptors_.size() ||
        animation_frame_delays_ms_.size() != animation_frame_descriptors_.size() || animation_canvas_.empty() || animation_canvas_width_ == 0 ||
        animation_canvas_height_ == 0 || animation_canvas_stride_ == 0) {
        return E_POINTER;
    }

    *out_bitmap = nullptr;
    if (frame_index == 0) {
        std::fill(animation_canvas_.begin(), animation_canvas_.end(), 0);
        animation_has_restore_canvas_ = false;
        animation_prev_disposal_ = 0;
        animation_prev_left_ = 0;
        animation_prev_top_ = 0;
        animation_prev_width_ = 0;
        animation_prev_height_ = 0;
    } else {
        if (animation_prev_disposal_ == 2) {
            ClearCanvasRect(animation_canvas_,
                            animation_canvas_width_,
                            animation_canvas_height_,
                            animation_prev_left_,
                            animation_prev_top_,
                            animation_prev_width_,
                            animation_prev_height_);
        } else if (animation_prev_disposal_ == 3 && animation_has_restore_canvas_ &&
                   animation_restore_canvas_.size() == animation_canvas_.size()) {
            animation_canvas_ = animation_restore_canvas_;
        }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = animation_decoder_->GetFrame(static_cast<UINT>(frame_index), &frame);
    if (FAILED(hr) || !frame) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    UINT decode_width = 0;
    UINT decode_height = 0;
    hr = frame->GetSize(&decode_width, &decode_height);
    if (FAILED(hr) || decode_width == 0 || decode_height == 0) {
        return FAILED(hr) ? hr : E_FAIL;
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

    const GifFrameDescriptor& desc = animation_frame_descriptors_[frame_index];
    if (desc.disposal == 3) {
        animation_restore_canvas_ = animation_canvas_;
        animation_has_restore_canvas_ = true;
    } else {
        animation_has_restore_canvas_ = false;
    }

    CompositeFrame(animation_canvas_,
                   animation_canvas_width_,
                   animation_canvas_height_,
                   frame_pixels,
                   decode_width,
                   decode_height,
                   desc.left,
                   desc.top);

    const D2D1_BITMAP_PROPERTIES1 bitmap_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap1> frame_bitmap;
    hr = d2d_context_->CreateBitmap(D2D1::SizeU(animation_canvas_width_, animation_canvas_height_),
                                    animation_canvas_.data(),
                                    animation_canvas_stride_,
                                    &bitmap_props,
                                    &frame_bitmap);
    if (FAILED(hr) || !frame_bitmap) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    animation_prev_disposal_ = desc.disposal;
    animation_prev_left_ = desc.left;
    animation_prev_top_ = desc.top;
    animation_prev_width_ = desc.width;
    animation_prev_height_ = desc.height;

    *out_bitmap = frame_bitmap.Detach();
    return S_OK;
}

HRESULT QmiApp::LoadSvgDocument(const fs::path& path,
                                  ID2D1SvgDocument** out_svg,
                                  float* out_width,
                                  float* out_height,
                                  std::vector<SvgTextRun>* out_texts) {
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

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    const std::string svg((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (svg.empty()) {
        return E_FAIL;
    }
    const std::string normalized_svg = NormalizeSvgLightDarkColors(svg);
    ParseSvgTextRuns(normalized_svg, out_texts);
    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamFromBytes(normalized_svg, &stream);
    if (FAILED(hr)) {
        return hr;
    }
    ComPtr<ID2D1SvgDocument> doc;
    // Use a neutral bootstrap viewport first, then normalize to the SVG's own geometry.
    hr = d2d_context5_->CreateSvgDocument(stream.Get(), D2D1::SizeF(1.0f, 1.0f), &doc);
    if (FAILED(hr)) {
        return hr;
    }

    D2D1_SIZE_F viewport = doc->GetViewportSize();
    float width = viewport.width;
    float height = viewport.height;

    ComPtr<ID2D1SvgElement> root;
    D2D1_SVG_VIEWBOX viewbox{};
    bool has_viewbox = false;
    doc->GetRoot(&root);
    if (root) {
        if (SUCCEEDED(root->GetAttributeValue(
                L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, &viewbox, sizeof(viewbox))) &&
            viewbox.width > 0.0f && viewbox.height > 0.0f) {
            has_viewbox = true;
        }
    }

    const bool viewport_invalid = width < 1.0e-3f || height < 1.0e-3f;
    const bool viewport_is_bootstrap = std::fabs(width - 1.0f) < 1.0e-3f && std::fabs(height - 1.0f) < 1.0e-3f;
    if (has_viewbox) {
        const float viewport_ratio = width / std::max(1.0f, height);
        const float viewbox_ratio = viewbox.width / std::max(1.0f, viewbox.height);
        const bool aspect_mismatch = std::fabs(viewport_ratio - viewbox_ratio) > 0.01f;
        if (viewport_invalid || viewport_is_bootstrap || aspect_mismatch) {
            width = viewbox.width;
            height = viewbox.height;
        }
    }

    if (width < 1.0f || height < 1.0f) {
        width = 1024.0f;
        height = 1024.0f;
    }
    doc->SetViewportSize(D2D1::SizeF(width, height));

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
