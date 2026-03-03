#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
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
#include <webp/decode.h>

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

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

class QmiApp;

namespace {
constexpr wchar_t kMainClassName[] = L"QmiMainWindowClass";
constexpr wchar_t kSettingsClassName[] = L"QmiSettingsWindowClass";
constexpr int kAppIconResourceId = 101;

constexpr UINT kMenuOpenFile = 1001;
constexpr UINT kMenuCopyImage = 1002;
constexpr UINT kMenuSettings = 1003;
constexpr UINT kMenuExit = 1004;
constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT_PTR kStartupScanTimerId = 2;
constexpr UINT_PTR kAnimationTimerId = 3;

constexpr int kCtrlFitOnSwitch = 2001;
constexpr int kCtrlSmoothSampling = 2002;
constexpr int kCtrlSettingsNav = 2100;
constexpr int kCtrlAssociationApply = 2200;
constexpr int kCtrlAssociationSelectAll = 2201;
constexpr int kCtrlAssociationClearAll = 2202;
constexpr int kCtrlAssociationCheckboxBase = 2300;
constexpr int kCtrlShortcutsTable = 2400;
constexpr int kSettingsWindowWidth = 700;
constexpr int kSettingsWindowHeight = 460;

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
constexpr float kFilmStripLargeScale = 1.08f;
constexpr float kFilmStripMediumScale = 1.00f;
constexpr float kFilmStripSmallScale = 0.74f;
constexpr float kFilmStripHoverScaleBoost = 0.13f;
constexpr float kFilmStripScaleLerp = 0.28f;

template <typename T>
T Clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

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

struct AssociationTypeOption {
    const wchar_t* extension = L"";
    const wchar_t* label = L"";
};

constexpr std::array<AssociationTypeOption, 10> kAssociationTypes = {{
    {L".jpg", L"JPG (*.jpg)"},
    {L".jpeg", L"JPEG (*.jpeg)"},
    {L".png", L"PNG (*.png)"},
    {L".bmp", L"BMP (*.bmp)"},
    {L".ico", L"ICO (*.ico)"},
    {L".webp", L"WebP (*.webp)"},
    {L".gif", L"GIF (*.gif)"},
    {L".heic", L"HEIC (*.heic)"},
    {L".heif", L"HEIF (*.heif)"},
    {L".svg", L"SVG (*.svg)"},
}};

int AssociationCheckboxControlId(size_t index) {
    return kCtrlAssociationCheckboxBase + static_cast<int>(index);
}

bool IsAssociationCheckboxControlId(int control_id) {
    const int lower = kCtrlAssociationCheckboxBase;
    const int upper = lower + static_cast<int>(kAssociationTypes.size());
    return control_id >= lower && control_id < upper;
}

RECT GetMonitorWorkAreaFromWindow(HWND hwnd) {
    RECT fallback = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    const HMONITOR monitor = MonitorFromWindow(hwnd ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return fallback;
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return fallback;
    }
    return mi.rcWork;
}

void InitializeShortcutsTable(HWND table_hwnd) {
    if (!table_hwnd) {
        return;
    }

    ListView_SetExtendedListViewStyleEx(
        table_hwnd, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);

    LVCOLUMNW column{};
    column.mask = LVCF_FMT | LVCF_TEXT;
    column.fmt = LVCFMT_LEFT;
    column.pszText = const_cast<LPWSTR>(L"\u6309\u952e");
    ListView_InsertColumn(table_hwnd, 0, &column);
    column.pszText = const_cast<LPWSTR>(L"\u529f\u80fd");
    ListView_InsertColumn(table_hwnd, 1, &column);

    struct ShortcutRow {
        const wchar_t* key = L"";
        const wchar_t* action = L"";
    };
    constexpr std::array<ShortcutRow, 3> kRows = {{
        {L"Left / Up", L"\u4e0a\u4e00\u5f20\u56fe\u7247"},
        {L"Right / Down", L"\u4e0b\u4e00\u5f20\u56fe\u7247"},
        {L"0", L"\u91cd\u7f6e\u7f29\u653e\u4e0e\u5e73\u79fb"},
    }};

    for (int i = 0; i < static_cast<int>(kRows.size()); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(kRows[i].key);
        const int index = ListView_InsertItem(table_hwnd, &item);
        if (index >= 0) {
            ListView_SetItemText(table_hwnd, index, 1, const_cast<LPWSTR>(kRows[i].action));
        }
    }
}

void ResizeShortcutsTableColumns(HWND table_hwnd, int table_width) {
    if (!table_hwnd) {
        return;
    }
    const int safe_width = std::max(200, table_width);
    const int key_width = Clamp((safe_width * 38) / 100, 110, safe_width - 100);
    const int action_width = std::max(90, safe_width - key_width - 2);
    ListView_SetColumnWidth(table_hwnd, 0, key_width);
    ListView_SetColumnWidth(table_hwnd, 1, action_width);
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

std::wstring GetModulePath() {
    std::wstring result;
    DWORD capacity = MAX_PATH;
    while (capacity <= 32768) {
        std::vector<wchar_t> buffer(capacity, L'\0');
        const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return L"";
        }
        if (copied < buffer.size() - 1) {
            result.assign(buffer.data(), copied);
            return result;
        }
        capacity *= 2;
    }
    return L"";
}

std::wstring BuildQmiProgId(const std::wstring& extension) {
    std::wstring cleaned = extension;
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), L'.'), cleaned.end());
    cleaned = ToLower(cleaned);
    return L"Qmi.Image." + cleaned;
}

bool WriteRegistryString(HKEY root, const std::wstring& subkey, const wchar_t* value_name, const std::wstring& value) {
    HKEY key = nullptr;
    LONG status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const DWORD size_bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    status = RegSetValueExW(key,
                            value_name,
                            0,
                            REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            size_bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool WriteRegistryEmptyValue(HKEY root, const std::wstring& subkey, const std::wstring& value_name) {
    HKEY key = nullptr;
    LONG status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    status = RegSetValueExW(key, value_name.c_str(), 0, REG_NONE, nullptr, 0);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

std::optional<std::wstring> ReadRegistryString(HKEY root, const std::wstring& subkey, const wchar_t* value_name) {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD size_bytes = 0;
    status = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &size_bytes);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::vector<wchar_t> buffer((size_bytes / sizeof(wchar_t)) + 1, L'\0');
    status = RegQueryValueExW(key,
                              value_name,
                              nullptr,
                              &type,
                              reinterpret_cast<BYTE*>(buffer.data()),
                              &size_bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

bool DeleteRegistryValue(HKEY root, const std::wstring& subkey, const wchar_t* value_name) {
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegDeleteValueW(key, value_name);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool RegistryStringEquals(const std::wstring& a, const std::wstring& b) {
    return ToLower(a) == ToLower(b);
}

bool IsExtensionAssociatedToQmi(const std::wstring& extension) {
    const std::wstring key_path = L"Software\\Classes\\" + extension;
    const std::optional<std::wstring> current_prog_id = ReadRegistryString(HKEY_CURRENT_USER, key_path, nullptr);
    if (!current_prog_id.has_value()) {
        return false;
    }
    return RegistryStringEquals(*current_prog_id, BuildQmiProgId(extension));
}

bool RemoveExtensionDefaultIfOwned(const std::wstring& extension, const std::wstring& prog_id) {
    const std::wstring key_path = L"Software\\Classes\\" + extension;
    const std::optional<std::wstring> current_prog_id = ReadRegistryString(HKEY_CURRENT_USER, key_path, nullptr);
    if (!current_prog_id.has_value() || !RegistryStringEquals(*current_prog_id, prog_id)) {
        return true;
    }
    return DeleteRegistryValue(HKEY_CURRENT_USER, key_path, nullptr);
}

bool ApplyQmiFileAssociations(const std::vector<bool>& checked, std::wstring* out_message) {
    if (checked.size() != kAssociationTypes.size()) {
        if (out_message) {
            *out_message = L"\u5173\u8054\u9879\u6570\u91cf\u5f02\u5e38\uff0c\u672a\u5e94\u7528\u66f4\u6539\u3002";
        }
        return false;
    }

    const std::wstring module_path = GetModulePath();
    if (module_path.empty()) {
        if (out_message) {
            *out_message = L"\u65e0\u6cd5\u83b7\u53d6 Qmi \u53ef\u6267\u884c\u6587\u4ef6\u8def\u5f84\u3002";
        }
        return false;
    }

    const std::wstring command = L"\"" + module_path + L"\" \"%1\"";
    const std::wstring icon_ref = L"\"" + module_path + L"\",0";

    if (!WriteRegistryString(HKEY_CURRENT_USER,
                             L"Software\\Classes\\Applications\\Qmi.exe",
                             L"FriendlyAppName",
                             L"Qmi") ||
        !WriteRegistryString(HKEY_CURRENT_USER,
                             L"Software\\Classes\\Applications\\Qmi.exe\\shell\\open\\command",
                             nullptr,
                             command) ||
        !WriteRegistryString(HKEY_CURRENT_USER,
                             L"Software\\Classes\\Applications\\Qmi.exe\\DefaultIcon",
                             nullptr,
                             icon_ref)) {
        if (out_message) {
            *out_message = L"\u5199\u5165\u5e94\u7528\u6ce8\u518c\u4fe1\u606f\u5931\u8d25\u3002";
        }
        return false;
    }

    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\Qmi.exe\\SupportedTypes");

    size_t enabled_count = 0;
    for (size_t i = 0; i < kAssociationTypes.size(); ++i) {
        const std::wstring extension = kAssociationTypes[i].extension;
        const std::wstring prog_id = BuildQmiProgId(extension);
        if (checked[i]) {
            const std::wstring prog_key = L"Software\\Classes\\" + prog_id;
            const std::wstring ext_key = L"Software\\Classes\\" + extension;
            if (!WriteRegistryString(HKEY_CURRENT_USER,
                                     prog_key,
                                     nullptr,
                                     L"Qmi image file (" + extension + L")") ||
                !WriteRegistryString(HKEY_CURRENT_USER, prog_key + L"\\DefaultIcon", nullptr, icon_ref) ||
                !WriteRegistryString(HKEY_CURRENT_USER, prog_key + L"\\shell\\open\\command", nullptr, command) ||
                !WriteRegistryString(HKEY_CURRENT_USER, ext_key, nullptr, prog_id) ||
                !WriteRegistryEmptyValue(HKEY_CURRENT_USER, ext_key + L"\\OpenWithProgids", prog_id) ||
                !WriteRegistryEmptyValue(HKEY_CURRENT_USER,
                                         L"Software\\Classes\\Applications\\Qmi.exe\\SupportedTypes",
                                         extension)) {
                if (out_message) {
                    *out_message = L"\u5199\u5165\u6269\u5c55\u540d\u5173\u8054\u5931\u8d25\uff1a" + extension;
                }
                return false;
            }
            ++enabled_count;
            continue;
        }

        const std::wstring ext_key = L"Software\\Classes\\" + extension;
        if (!RemoveExtensionDefaultIfOwned(extension, prog_id) ||
            !DeleteRegistryValue(HKEY_CURRENT_USER, ext_key + L"\\OpenWithProgids", prog_id.c_str())) {
            if (out_message) {
                *out_message = L"\u79fb\u9664\u6269\u5c55\u540d\u5173\u8054\u5931\u8d25\uff1a" + extension;
            }
            return false;
        }

        SHDeleteKeyW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + prog_id).c_str());
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        reinterpret_cast<LPARAM>(L"Software\\Classes"),
                        SMTO_ABORTIFHUNG,
                        1500,
                        nullptr);

    if (out_message) {
        *out_message =
            L"\u5df2\u5e94\u7528 " + std::to_wstring(enabled_count) +
            L" \u79cd\u6587\u4ef6\u7c7b\u578b\u5173\u8054\u3002\u82e5\u7cfb\u7edf\u4ecd\u672a\u5207\u6362\uff0c\u8bf7\u5728 Windows \u9ed8\u8ba4\u5e94\u7528\u4e2d\u5c06\u5bf9\u5e94\u683c\u5f0f\u8bbe\u4e3a Qmi\u3002";
    }
    return true;
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
    Broken,
    Raster,
    Svg
};

bool IsRenderableImageType(ImageType type) {
    return type == ImageType::Raster || type == ImageType::Svg;
}

enum class TitleButton {
    None,
    Minimize,
    Maximize,
    Close
};

enum class EdgeNavButton {
    None,
    Previous,
    Next
};

struct TitleButtons {
    RECT min_rect{};
    RECT max_rect{};
    RECT close_rect{};
};

struct EdgeNavButtons {
    D2D1_RECT_F prev_rect{};
    D2D1_RECT_F next_rect{};
    D2D1_RECT_F left_trigger_rect{};
    D2D1_RECT_F right_trigger_rect{};
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

struct GifFrameDescriptor {
    UINT left = 0;
    UINT top = 0;
    UINT width = 0;
    UINT height = 0;
    UINT disposal = 0;
};

enum class SettingsPage {
    General = 0,
    Associations = 1,
    Shortcuts = 2,
    About = 3
};

struct SettingsWindowState {
    QmiApp* app = nullptr;
    int active_page = static_cast<int>(SettingsPage::General);

    HWND nav_list = nullptr;
    HWND nav_divider = nullptr;

    HWND fit_checkbox = nullptr;
    HWND smooth_checkbox = nullptr;

    HWND associations_hint = nullptr;
    std::vector<HWND> association_checkboxes;
    HWND association_select_all_button = nullptr;
    HWND association_clear_all_button = nullptr;
    HWND association_apply_button = nullptr;
    HWND association_status = nullptr;

    HWND shortcuts_table = nullptr;
    HWND about_text = nullptr;

    HFONT nav_font = nullptr;
    HFONT body_font = nullptr;
};

void SetControlFont(HWND hwnd, HFONT font) {
    if (!hwnd || !font) {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void SetAssociationStatus(SettingsWindowState* state, const std::wstring& text) {
    if (!state || !state->association_status) {
        return;
    }

    HWND status_hwnd = state->association_status;
    SetWindowTextW(status_hwnd, text.c_str());

    HWND parent_hwnd = GetParent(status_hwnd);
    RECT status_rect{};
    if (parent_hwnd && GetWindowRect(status_hwnd, &status_rect)) {
        MapWindowPoints(nullptr, parent_hwnd, reinterpret_cast<POINT*>(&status_rect), 2);
        RedrawWindow(parent_hwnd, &status_rect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
    RedrawWindow(status_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void SyncAssociationSelections(SettingsWindowState* state) {
    if (!state) {
        return;
    }
    for (size_t i = 0; i < state->association_checkboxes.size() && i < kAssociationTypes.size(); ++i) {
        const bool is_checked = IsExtensionAssociatedToQmi(kAssociationTypes[i].extension);
        SendMessageW(state->association_checkboxes[i], BM_SETCHECK, is_checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void SetAllAssociationSelections(SettingsWindowState* state, bool checked) {
    if (!state) {
        return;
    }
    for (HWND checkbox : state->association_checkboxes) {
        SendMessageW(checkbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool ApplyAssociationSelectionFromUi(SettingsWindowState* state, std::wstring* out_message) {
    if (!state) {
        if (out_message) {
            *out_message = L"\u8bbe\u7f6e\u7a97\u53e3\u72b6\u6001\u4e0d\u53ef\u7528\u3002";
        }
        return false;
    }

    std::vector<bool> checked;
    checked.reserve(state->association_checkboxes.size());
    for (HWND checkbox : state->association_checkboxes) {
        checked.push_back(SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    return ApplyQmiFileAssociations(checked, out_message);
}

void SetActiveSettingsPage(SettingsWindowState* state, int page_index) {
    if (!state) {
        return;
    }
    state->active_page =
        Clamp(page_index, static_cast<int>(SettingsPage::General), static_cast<int>(SettingsPage::About));
    const bool show_general = state->active_page == static_cast<int>(SettingsPage::General);
    const bool show_associations = state->active_page == static_cast<int>(SettingsPage::Associations);
    const bool show_shortcuts = state->active_page == static_cast<int>(SettingsPage::Shortcuts);
    const bool show_about = state->active_page == static_cast<int>(SettingsPage::About);

    ShowWindow(state->fit_checkbox, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->smooth_checkbox, show_general ? SW_SHOW : SW_HIDE);

    ShowWindow(state->associations_hint, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_select_all_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_clear_all_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_apply_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_status, show_associations ? SW_SHOW : SW_HIDE);
    for (HWND checkbox : state->association_checkboxes) {
        ShowWindow(checkbox, show_associations ? SW_SHOW : SW_HIDE);
    }

    ShowWindow(state->shortcuts_table, show_shortcuts ? SW_SHOW : SW_HIDE);
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
    constexpr int kNavWidth = 112;
    constexpr int kNavDividerGap = 12;
    constexpr int kDividerWidth = 2;
    constexpr int kDividerPanelGap = 12;
    constexpr int kPanelPaddingX = 16;

    const int nav_height = std::max(120, client_height - (kOuterPadding * 2));
    MoveWindow(state->nav_list, kOuterPadding, kOuterPadding, kNavWidth, nav_height, TRUE);

    const int divider_x = kOuterPadding + kNavWidth + kNavDividerGap;
    if (state->nav_divider) {
        MoveWindow(state->nav_divider, divider_x, kOuterPadding, kDividerWidth, nav_height, TRUE);
    }

    const int panel_x = divider_x + kDividerWidth + kDividerPanelGap;
    const int panel_width = std::max(160, client_width - panel_x - kOuterPadding);
    const int panel_y = kOuterPadding;
    const int panel_height = std::max(120, client_height - (kOuterPadding * 2));

    const int text_width = std::max(80, panel_width - kPanelPaddingX * 2);
    MoveWindow(state->fit_checkbox, panel_x + kPanelPaddingX, panel_y + 8, text_width, 28, TRUE);
    MoveWindow(state->smooth_checkbox, panel_x + kPanelPaddingX, panel_y + 44, text_width, 28, TRUE);

    const int association_hint_y = panel_y + 8;
    MoveWindow(state->associations_hint, panel_x + kPanelPaddingX, association_hint_y, text_width, 44, TRUE);

    constexpr int kAssociationColumns = 2;
    constexpr int kAssociationRowHeight = 30;
    constexpr int kAssociationColGap = 16;
    const int association_cell_width = std::max(90, (text_width - kAssociationColGap) / kAssociationColumns);
    const int association_grid_y = association_hint_y + 52;
    for (size_t i = 0; i < state->association_checkboxes.size(); ++i) {
        const int col = static_cast<int>(i % kAssociationColumns);
        const int row = static_cast<int>(i / kAssociationColumns);
        const int x = panel_x + kPanelPaddingX + col * (association_cell_width + kAssociationColGap);
        const int y = association_grid_y + row * kAssociationRowHeight;
        MoveWindow(state->association_checkboxes[i], x, y, association_cell_width, 24, TRUE);
    }

    const int association_rows = static_cast<int>((state->association_checkboxes.size() + kAssociationColumns - 1) /
                                                  kAssociationColumns);
    const int button_y = association_grid_y + association_rows * kAssociationRowHeight + 4;
    constexpr int kAssociationButtonGap = 10;
    const int button_width = std::max(1, (text_width - kAssociationButtonGap * 2) / 3);
    const int button2_x = panel_x + kPanelPaddingX + button_width + kAssociationButtonGap;
    const int button3_x = button2_x + button_width + kAssociationButtonGap;
    MoveWindow(state->association_select_all_button, panel_x + kPanelPaddingX, button_y, button_width, 30, TRUE);
    MoveWindow(state->association_clear_all_button, button2_x, button_y, button_width, 30, TRUE);
    MoveWindow(state->association_apply_button, button3_x, button_y, button_width, 30, TRUE);
    MoveWindow(state->association_status,
               panel_x + kPanelPaddingX,
               button_y + 38,
               text_width,
               std::max(34, panel_height - (button_y - panel_y) - 42),
               TRUE);

    MoveWindow(state->about_text, panel_x + kPanelPaddingX, panel_y + 8, text_width, std::max(40, panel_height - 16), TRUE);
    MoveWindow(
        state->shortcuts_table, panel_x + kPanelPaddingX, panel_y + 8, text_width, std::max(40, panel_height - 16), TRUE);
    ResizeShortcutsTableColumns(state->shortcuts_table, text_width);
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
    void UpdateCurrentImageInfo();
    void ClearCurrentImageInfo();

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
    HRESULT DecodeGifFrame(size_t frame_index, ID2D1Bitmap1** out_bitmap);
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
    void DrawTopInfoBar(const TitleButtons& buttons);
    void DrawTitleButtons(const TitleButtons& buttons);
    void DrawEdgeNavButtons(const D2D1_RECT_F& viewport);
    void DrawOpenButton(const D2D1_RECT_F& viewport);
    void DrawBrokenImagePlaceholder(const D2D1_RECT_F& viewport);
    void DrawButtonGlyph(TitleButton button, const RECT& rect);
    void DrawCenteredText(const std::wstring& text, const D2D1_RECT_F& rect, IDWriteTextFormat* format);

    D2D1_RECT_F GetImageViewport(float width, float height) const;
    D2D1_RECT_F GetFilmStripRect(float width, float height) const;
    float GetBaseImageScale(const D2D1_RECT_F& viewport) const;
    D2D1_RECT_F GetImageDestinationRect(const D2D1_RECT_F& viewport) const;
    D2D1_RECT_F GetOpenButtonRect(const D2D1_RECT_F& viewport) const;
    TitleButtons GetTitleButtons() const;
    EdgeNavButtons GetEdgeNavButtons(const D2D1_RECT_F& viewport) const;
    TitleButton HitTestTitleButton(POINT client_pt) const;
    EdgeNavButton HitTestEdgeNavButton(POINT client_pt) const;
    EdgeNavButton HitTestEdgeNavTrigger(POINT client_pt) const;
    bool HitTestOpenButton(POINT client_pt) const;
    bool IsPointOverVisibleImage(POINT client_pt, const D2D1_RECT_F& viewport) const;
    int HitTestThumbnail(POINT client_pt) const;

    void ShowContextMenu(POINT screen_pt);
    void OpenFileDialog();
    void OpenSettingsWindow();
    bool CopyCurrentImageToClipboard();
    HRESULT ExtractCurrentImagePixels(UINT32* out_width,
                                      UINT32* out_height,
                                      std::vector<std::uint8_t>* out_pixels);
    HRESULT ReadBitmapPixels(ID2D1Bitmap1* source_bitmap,
                             UINT32* out_width,
                             UINT32* out_height,
                             std::vector<std::uint8_t>* out_pixels);
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
    ComPtr<IDWriteTextFormat> info_text_format_;

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
    std::vector<float> thumbnail_draw_scales_;
    std::vector<VisibleThumb> visible_thumbs_;

    LoadedImage current_image_;
    int current_index_ = -1;
    std::wstring current_error_;
    std::wstring current_image_info_;

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
    EdgeNavButton hover_edge_nav_button_ = EdgeNavButton::None;
    EdgeNavButton visible_edge_nav_button_ = EdgeNavButton::None;
    EdgeNavButton pressed_edge_nav_button_ = EdgeNavButton::None;
    int hover_thumbnail_index_ = -1;
    bool hover_open_button_ = false;
    bool pressed_open_button_ = false;
    bool render_timer_armed_ = false;
    ULONGLONG last_interactive_render_tick_ = 0;
    bool deferred_directory_build_pending_ = false;
    std::wstring deferred_directory_target_norm_;
    bool bitmaps_need_reload_ = false;
    ComPtr<IWICBitmapDecoder> animation_decoder_;
    std::vector<GifFrameDescriptor> animation_frame_descriptors_;
    std::vector<UINT> animation_frame_delays_ms_;
    size_t animation_frame_index_ = 0;
    std::vector<std::uint8_t> animation_canvas_;
    std::vector<std::uint8_t> animation_restore_canvas_;
    bool animation_has_restore_canvas_ = false;
    UINT animation_canvas_width_ = 0;
    UINT animation_canvas_height_ = 0;
    UINT animation_canvas_stride_ = 0;
    UINT animation_prev_disposal_ = 0;
    UINT animation_prev_left_ = 0;
    UINT animation_prev_top_ = 0;
    UINT animation_prev_width_ = 0;
    UINT animation_prev_height_ = 0;

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

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES;
    if (!InitCommonControlsEx(&common_controls)) {
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
    thumbnail_draw_scales_.assign(images_.size(), 0.0f);
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
    if (IsRenderableImageType(current_image_.type)) {
        const int pixel_w = std::max(1, static_cast<int>(std::lround(current_image_.width)));
        const int pixel_h = std::max(1, static_cast<int>(std::lround(current_image_.height)));
        resolution = std::to_wstring(pixel_w) + L"x" + std::to_wstring(pixel_h);
    } else if (current_image_.type == ImageType::Broken) {
        resolution = L"损坏/无法解码";
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
    if (index != current_index_) {
        film_strip_scroll_index_ = -1;
    }
    current_index_ = index;
    UpdateCurrentImageInfo();
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
    if (!IsRenderableImageType(current_image_.type)) {
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
    if (!IsRenderableImageType(current_image_.type)) {
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

TitleButtons QmiApp::GetTitleButtons() const {
    TitleButtons b{};

    if (!hwnd_) {
        return b;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int top = rc.top;
    const int right = rc.right;

    b.close_rect = RECT{right - kTitleButtonWidth, top, right, top + kTitleButtonHeight};
    b.max_rect = RECT{right - kTitleButtonWidth * 2, top, right - kTitleButtonWidth, top + kTitleButtonHeight};
    b.min_rect = RECT{right - kTitleButtonWidth * 3, top, right - kTitleButtonWidth * 2, top + kTitleButtonHeight};

    return b;
}

EdgeNavButtons QmiApp::GetEdgeNavButtons(const D2D1_RECT_F& viewport) const {
    EdgeNavButtons buttons{};
    const float view_w = std::max(1.0f, viewport.right - viewport.left);
    const float view_h = std::max(1.0f, viewport.bottom - viewport.top);
    const float button_size = Clamp(std::min(view_w, view_h) * 0.105f, 46.0f, 64.0f);
    const float side_inset = 14.0f;
    const float trigger_width = std::max(76.0f, button_size + 18.0f);
    const float center_y = (viewport.top + viewport.bottom) * 0.5f;

    const float prev_left = viewport.left + side_inset;
    const float next_right = viewport.right - side_inset;
    buttons.prev_rect = D2D1::RectF(prev_left, center_y - button_size * 0.5f, prev_left + button_size, center_y + button_size * 0.5f);
    buttons.next_rect =
        D2D1::RectF(next_right - button_size, center_y - button_size * 0.5f, next_right, center_y + button_size * 0.5f);

    buttons.left_trigger_rect = D2D1::RectF(viewport.left, viewport.top, viewport.left + trigger_width, viewport.bottom);
    buttons.right_trigger_rect = D2D1::RectF(viewport.right - trigger_width, viewport.top, viewport.right, viewport.bottom);
    return buttons;
}

TitleButton QmiApp::HitTestTitleButton(POINT client_pt) const {
    if (!hwnd_) {
        return TitleButton::None;
    }

    const TitleButtons buttons = GetTitleButtons();

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

EdgeNavButton QmiApp::HitTestEdgeNavButton(POINT client_pt) const {
    if (!hwnd_ || current_image_.type == ImageType::None || images_.size() <= 1) {
        return EdgeNavButton::None;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F viewport = GetImageViewport(static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
    const EdgeNavButtons buttons = GetEdgeNavButtons(viewport);
    const float x = static_cast<float>(client_pt.x);
    const float y = static_cast<float>(client_pt.y);
    if (x >= buttons.prev_rect.left && x <= buttons.prev_rect.right && y >= buttons.prev_rect.top && y <= buttons.prev_rect.bottom) {
        return EdgeNavButton::Previous;
    }
    if (x >= buttons.next_rect.left && x <= buttons.next_rect.right && y >= buttons.next_rect.top && y <= buttons.next_rect.bottom) {
        return EdgeNavButton::Next;
    }
    return EdgeNavButton::None;
}

EdgeNavButton QmiApp::HitTestEdgeNavTrigger(POINT client_pt) const {
    if (!hwnd_ || current_image_.type == ImageType::None || images_.size() <= 1) {
        return EdgeNavButton::None;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_RECT_F viewport = GetImageViewport(static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
    const float x = static_cast<float>(client_pt.x);
    const float y = static_cast<float>(client_pt.y);
    if (x < viewport.left || x > viewport.right || y < viewport.top || y > viewport.bottom) {
        return EdgeNavButton::None;
    }

    const EdgeNavButtons buttons = GetEdgeNavButtons(viewport);
    if (x >= buttons.left_trigger_rect.left && x <= buttons.left_trigger_rect.right && y >= buttons.left_trigger_rect.top &&
        y <= buttons.left_trigger_rect.bottom) {
        return EdgeNavButton::Previous;
    }
    if (x >= buttons.right_trigger_rect.left && x <= buttons.right_trigger_rect.right && y >= buttons.right_trigger_rect.top &&
        y <= buttons.right_trigger_rect.bottom) {
        return EdgeNavButton::Next;
    }
    return EdgeNavButton::None;
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
    if (!IsRenderableImageType(current_image_.type)) {
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
    d2d_context_->DrawTextW(current_image_info_.c_str(),
                            static_cast<UINT32>(current_image_info_.size()),
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

    DrawCenteredText(L"\u6253\u5f00\u56fe\u7247...", button_rect, text_format_.Get());
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
        const D2D1_RECT_F button_rect = GetOpenButtonRect(viewport);
        const float hint_top = std::min(viewport.bottom - 38.0f, button_rect.bottom + 12.0f);
        DrawCenteredText(L"\u6216\u62d6\u653e\u4e00\u4e2a\u56fe\u7247\u6587\u4ef6\u3002", D2D1::RectF(viewport.left + 20.0f, hint_top, viewport.right - 20.0f, hint_top + 26.0f), small_text_format_.Get());
        return;
    }
    if (current_image_.type == ImageType::Broken) {
        DrawBrokenImagePlaceholder(viewport);
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

void QmiApp::ShowContextMenu(POINT screen_pt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    const bool can_copy_image = IsRenderableImageType(current_image_.type);
    AppendMenuW(menu, MF_STRING, kMenuOpenFile, L"\u6253\u5f00...");
    AppendMenuW(menu, can_copy_image ? MF_STRING : (MF_STRING | MF_GRAYED), kMenuCopyImage, L"\u590d\u5236\u56fe\u7247");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"\u8bbe\u7f6e...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"\u9000\u51fa");

    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case kMenuOpenFile:
            OpenFileDialog();
            break;
        case kMenuCopyImage:
            CopyCurrentImageToClipboard();
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
    const EdgeNavButton hovered_nav_button = HitTestEdgeNavButton(client_pt);
    const EdgeNavButton visible_nav_button = HitTestEdgeNavTrigger(client_pt);
    const int hovered_thumb_index = HitTestThumbnail(client_pt);
    if (hovered != hover_button_ || hovered_open_button != hover_open_button_ || hovered_nav_button != hover_edge_nav_button_ ||
        visible_nav_button != visible_edge_nav_button_ || hovered_thumb_index != hover_thumbnail_index_) {
        hover_button_ = hovered;
        hover_open_button_ = hovered_open_button;
        hover_edge_nav_button_ = hovered_nav_button;
        visible_edge_nav_button_ = visible_nav_button;
        hover_thumbnail_index_ = hovered_thumb_index;
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
        if (images_.empty() || current_index_ < 0) {
            return;
        }

        int notches = static_cast<int>(wheel_delta / WHEEL_DELTA);
        if (notches == 0) {
            if (wheel_delta == 0) {
                return;
            }
            notches = (wheel_delta > 0) ? 1 : -1;
        }
        if (notches == 0) {
            return;
        }
        film_strip_scroll_index_ = -1;
        MoveSelection(-notches);
        return;
    }

    if (!IsRenderableImageType(current_image_.type)) {
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
                case kMenuCopyImage:
                    CopyCurrentImageToClipboard();
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
            SendMessageW(state->fit_checkbox, BM_SETCHECK, app && app->fit_on_switch_ ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(state->smooth_checkbox,
                         BM_SETCHECK,
                         app && app->smooth_sampling_ ? BST_CHECKED : BST_UNCHECKED,
                         0);

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

            state->about_text = CreateWindowExW(0,
                                                L"STATIC",
                                                L"Qmi\r\n\r\n\u8f7b\u91cf\u7ea7 Windows \u770b\u56fe\u5de5\u5177\u3002\r\n\u652f\u6301\u683c\u5f0f\uff1ajpg / jpeg / png / bmp / ico / webp / gif / heic / heif / svg",
                                                WS_CHILD | WS_VISIBLE,
                                                0,
                                                0,
                                                0,
                                                0,
                                                hwnd,
                                                nullptr,
                                                nullptr,
                                                nullptr);
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
            SetControlFont(state->associations_hint, state->body_font);
            for (HWND checkbox : state->association_checkboxes) {
                SetControlFont(checkbox, state->body_font);
            }
            SetControlFont(state->association_select_all_button, state->body_font);
            SetControlFont(state->association_clear_all_button, state->body_font);
            SetControlFont(state->association_apply_button, state->body_font);
            SetControlFont(state->association_status, state->body_font);
            SetControlFont(state->shortcuts_table, state->body_font);
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
                if (ctrl == state->association_select_all_button || ctrl == state->association_clear_all_button ||
                    ctrl == state->association_apply_button) {
                    break;
                }
                HDC hdc = reinterpret_cast<HDC>(wparam);
                bool is_association_checkbox = false;
                for (HWND checkbox : state->association_checkboxes) {
                    if (ctrl == checkbox) {
                        is_association_checkbox = true;
                        break;
                    }
                }

                const bool is_panel_ctrl = ctrl == state->fit_checkbox || ctrl == state->smooth_checkbox ||
                                           ctrl == state->associations_hint || ctrl == state->association_status ||
                                           ctrl == state->about_text || is_association_checkbox;
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
