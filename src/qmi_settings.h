#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <array>
#include <string>
#include <vector>

class QmiApp;

inline constexpr wchar_t kSettingsClassName[] = L"QmiSettingsWindowClass";
inline constexpr UINT kMenuSettings = 1005;

inline constexpr int kCtrlSettingsNav = 2100;
inline constexpr int kCtrlAssociationApply = 2200;
inline constexpr int kCtrlAssociationSelectAll = 2201;
inline constexpr int kCtrlAssociationClearAll = 2202;
inline constexpr int kCtrlAssociationCheckboxBase = 2300;
inline constexpr int kCtrlShortcutsTable = 2400;
inline constexpr int kCtrlAboutRepoLink = 2500;

inline constexpr int kSettingsWindowWidth = 700;
inline constexpr int kSettingsWindowHeight = 460;

inline constexpr wchar_t kQmiAuthorName[] = L"fonlan";
inline constexpr wchar_t kQmiRepositoryUrl[] = L"https://github.com/fonlan/Qmi";

struct AssociationTypeOption {
    const wchar_t* extension = L"";
    const wchar_t* label = L"";
};

inline constexpr std::array<AssociationTypeOption, 10> kAssociationTypes = {{
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
    HWND about_icon = nullptr;
    HWND about_title = nullptr;
    HWND about_description = nullptr;
    HWND about_author = nullptr;
    HWND about_repo_label = nullptr;
    HWND about_repo_link = nullptr;

    HFONT nav_font = nullptr;
    HFONT body_font = nullptr;
    HFONT about_title_font = nullptr;
    HFONT about_link_font = nullptr;
};

int AssociationCheckboxControlId(size_t index);
bool IsAssociationCheckboxControlId(int control_id);
RECT GetMonitorWorkAreaFromWindow(HWND hwnd);
void InitializeShortcutsTable(HWND table_hwnd);
void ResizeShortcutsTableColumns(HWND table_hwnd, int table_width);
void SetControlFont(HWND hwnd, HFONT font);
void SetAssociationStatus(SettingsWindowState* state, const std::wstring& text);
void SyncAssociationSelections(SettingsWindowState* state);
void SetAllAssociationSelections(SettingsWindowState* state, bool checked);
bool ApplyAssociationSelectionFromUi(SettingsWindowState* state, std::wstring* out_message);
void SetActiveSettingsPage(SettingsWindowState* state, int page_index);
void LayoutSettingsWindow(HWND hwnd, SettingsWindowState* state);
