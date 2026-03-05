#include "qmi_settings.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

namespace {
std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return s;
}

std::wstring BuildQmiProgId(const std::wstring& extension) {
    std::wstring cleaned = extension;
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), L'.'), cleaned.end());
    cleaned = ToLower(cleaned);
    return L"Qmi.Image." + cleaned;
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

int MeasureTextWidthForControl(HWND control, const wchar_t* text) {
    if (!control || !text || *text == L'\0') {
        return 0;
    }
    HDC dc = GetDC(control);
    if (!dc) {
        return 0;
    }
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = nullptr;
    if (font) {
        old_font = SelectObject(dc, font);
    }
    SIZE text_size{};
    const int text_len = lstrlenW(text);
    if (!GetTextExtentPoint32W(dc, text, text_len, &text_size)) {
        text_size.cx = 0;
    }
    if (old_font) {
        SelectObject(dc, old_font);
    }
    ReleaseDC(control, dc);
    return text_size.cx;
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
        *out_message = L"\u5df2\u5e94\u7528 " + std::to_wstring(enabled_count) +
                       L" \u79cd\u6587\u4ef6\u7c7b\u578b\u5173\u8054\u3002\u82e5\u7cfb\u7edf\u4ecd\u672a\u5207\u6362\uff0c\u8bf7\u5728 Windows \u9ed8\u8ba4\u5e94\u7528\u4e2d\u5c06\u5bf9\u5e94\u683c\u5f0f\u8bbe\u4e3a Qmi\u3002";
    }
    return true;
}
}  // namespace

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
    constexpr std::array<ShortcutRow, 10> kRows = {{
        {L"Left", L"\u4e0a\u4e00\u5f20\u56fe\u7247"},
        {L"Right", L"\u4e0b\u4e00\u5f20\u56fe\u7247"},
        {L"Up", L"\u653e\u5927\u89c6\u56fe"},
        {L"Down", L"\u7f29\u5c0f\u89c6\u56fe"},
        {L"Esc", L"\u9000\u51fa\u7a0b\u5e8f"},
        {L"0", L"\u91cd\u7f6e\u7f29\u653e\u4e0e\u5e73\u79fb"},
        {L"Ctrl + O", L"\u6253\u5f00\u56fe\u7247"},
        {L"Ctrl + C", L"\u590d\u5236\u5f53\u524d\u56fe\u7247"},
        {L"Ctrl + P", L"\u6253\u5370\u5f53\u524d\u56fe\u7247"},
        {L"Delete", L"\u5220\u9664\u5f53\u524d\u6587\u4ef6\uff08\u79fb\u5165\u56de\u6536\u7ad9\uff09"},
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
    const int key_width = std::clamp((safe_width * 38) / 100, 110, safe_width - 100);
    const int action_width = std::max(90, safe_width - key_width - 2);
    ListView_SetColumnWidth(table_hwnd, 0, key_width);
    ListView_SetColumnWidth(table_hwnd, 1, action_width);
}

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
        std::clamp(page_index, static_cast<int>(SettingsPage::General), static_cast<int>(SettingsPage::About));
    const bool show_general = state->active_page == static_cast<int>(SettingsPage::General);
    const bool show_associations = state->active_page == static_cast<int>(SettingsPage::Associations);
    const bool show_shortcuts = state->active_page == static_cast<int>(SettingsPage::Shortcuts);
    const bool show_about = state->active_page == static_cast<int>(SettingsPage::About);

    ShowWindow(state->fit_checkbox, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->smooth_checkbox, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->opacity_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->opacity_slider, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->opacity_value_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->background_color_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->background_color_preview, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->background_color_button, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->startup_monitor_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->startup_monitor_combo, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->sort_field_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->sort_field_combo, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->sort_direction_label, show_general ? SW_SHOW : SW_HIDE);
    ShowWindow(state->sort_direction_combo, show_general ? SW_SHOW : SW_HIDE);

    ShowWindow(state->associations_hint, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_select_all_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_clear_all_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_apply_button, show_associations ? SW_SHOW : SW_HIDE);
    ShowWindow(state->association_status, show_associations ? SW_SHOW : SW_HIDE);
    for (HWND checkbox : state->association_checkboxes) {
        ShowWindow(checkbox, show_associations ? SW_SHOW : SW_HIDE);
    }

    ShowWindow(state->shortcuts_table, show_shortcuts ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_icon_border, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_icon, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_title, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_description, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_author, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_repo_label, show_about ? SW_SHOW : SW_HIDE);
    ShowWindow(state->about_repo_link, show_about ? SW_SHOW : SW_HIDE);
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
    const int general_x = panel_x + kPanelPaddingX;
    const int opacity_label_y = panel_y + 8;
    const int opacity_slider_y = opacity_label_y + 24;
    constexpr int kOpacityValueWidth = 56;
    constexpr int kOpacityValueGap = 8;
    const int opacity_slider_width = std::max(80, text_width - kOpacityValueWidth - kOpacityValueGap);
    constexpr int kColorPreviewWidth = 48;
    constexpr int kColorPreviewHeight = 24;
    constexpr int kColorButtonWidth = 110;
    constexpr int kColorGap = 10;
    const int color_row_y = panel_y + 102;
    const int color_preview_y = color_row_y;
    const int color_label_y = color_row_y + 1;
    const int color_button_x = std::max(general_x, general_x + text_width - kColorButtonWidth);
    const int color_preview_x = std::max(general_x, color_button_x - kColorGap - kColorPreviewWidth);
    const int color_label_width = std::max(1, color_preview_x - general_x - kColorGap);

    MoveWindow(state->opacity_label, general_x, opacity_label_y, text_width, 22, TRUE);
    MoveWindow(state->opacity_slider, general_x, opacity_slider_y, opacity_slider_width, 30, TRUE);
    MoveWindow(state->opacity_value_label,
               general_x + opacity_slider_width + kOpacityValueGap,
               opacity_slider_y + 4,
               kOpacityValueWidth,
               22,
               TRUE);
    MoveWindow(state->background_color_label, general_x, color_label_y, color_label_width, 22, TRUE);
    MoveWindow(state->background_color_preview, color_preview_x, color_preview_y, kColorPreviewWidth, kColorPreviewHeight, TRUE);
    MoveWindow(state->background_color_button, color_button_x, color_preview_y - 1, kColorButtonWidth, 26, TRUE);
    MoveWindow(state->fit_checkbox, general_x, panel_y + 140, text_width, 28, TRUE);
    MoveWindow(state->smooth_checkbox, general_x, panel_y + 176, text_width, 28, TRUE);
    const int startup_monitor_label_y = panel_y + 214;
    const int startup_monitor_combo_y = startup_monitor_label_y + 24;
    MoveWindow(state->startup_monitor_label, general_x, startup_monitor_label_y, text_width, 22, TRUE);
    MoveWindow(state->startup_monitor_combo, general_x, startup_monitor_combo_y, text_width, 120, TRUE);
    const int sort_field_label_y = startup_monitor_combo_y + 38;
    const int sort_field_combo_y = sort_field_label_y + 24;
    const int sort_direction_label_y = sort_field_combo_y + 36;
    const int sort_direction_combo_y = sort_direction_label_y + 24;
    MoveWindow(state->sort_field_label, general_x, sort_field_label_y, text_width, 22, TRUE);
    MoveWindow(state->sort_field_combo, general_x, sort_field_combo_y, text_width, 220, TRUE);
    MoveWindow(state->sort_direction_label, general_x, sort_direction_label_y, text_width, 22, TRUE);
    MoveWindow(state->sort_direction_combo, general_x, sort_direction_combo_y, text_width, 120, TRUE);

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

    const int about_x = panel_x + kPanelPaddingX;
    const int about_y = panel_y + 16;
    constexpr int kAboutIconSize = 64;
    const int about_icon_frame_size = kAboutIconSize + (kAboutIconBorderThickness * 2);
    constexpr int kAboutIconTitleGap = 14;
    constexpr int kAboutTitleHeight = 52;
    const int about_title_x = about_x + about_icon_frame_size + kAboutIconTitleGap;
    const int about_title_width = std::max(80, text_width - about_icon_frame_size - kAboutIconTitleGap);
    const int about_title_y = about_y + std::max(0, (about_icon_frame_size - kAboutTitleHeight) / 2);

    MoveWindow(state->about_icon_border, about_x, about_y, about_icon_frame_size, about_icon_frame_size, TRUE);
    MoveWindow(state->about_icon,
               about_x + kAboutIconBorderThickness,
               about_y + kAboutIconBorderThickness,
               kAboutIconSize,
               kAboutIconSize,
               TRUE);
    MoveWindow(state->about_title, about_title_x, about_title_y, about_title_width, kAboutTitleHeight, TRUE);

    const int about_desc_y = about_y + kAboutIconSize + 16;
    MoveWindow(state->about_description, about_x, about_desc_y, text_width, 48, TRUE);

    const int about_author_y = about_desc_y + 58;
    MoveWindow(state->about_author, about_x, about_author_y, text_width, 26, TRUE);

    const int about_repo_y = about_author_y + 34;
    const int about_repo_label_text_width =
        MeasureTextWidthForControl(state->about_repo_label, L"\u4ed3\u5e93\uff1a");
    const int about_repo_label_width = std::clamp(about_repo_label_text_width + 10, 46, 96);
    const int repo_link_char_width = std::max(1, MeasureTextWidthForControl(state->about_repo_link, L"0"));
    const int repo_link_left_shift = std::clamp(repo_link_char_width, 6, 16);
    const int about_repo_link_offset = std::max(0, about_repo_label_width - repo_link_left_shift);
    MoveWindow(state->about_repo_label, about_x, about_repo_y, about_repo_label_width, 26, TRUE);
    MoveWindow(state->about_repo_link,
               about_x + about_repo_link_offset,
               about_repo_y,
               std::max(60, text_width - about_repo_link_offset),
               26,
               TRUE);
    MoveWindow(
        state->shortcuts_table, panel_x + kPanelPaddingX, panel_y + 8, text_width, std::max(40, panel_height - 16), TRUE);
    ResizeShortcutsTableColumns(state->shortcuts_table, text_width);
}
