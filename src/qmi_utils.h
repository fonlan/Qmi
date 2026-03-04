#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

std::uint8_t UnpremultiplyChannel(std::uint8_t value, std::uint8_t alpha);

std::wstring ToLower(std::wstring s);
std::wstring NormalizePathLower(const std::filesystem::path& p);

std::wstring FormatFileSizeText(ULONGLONG bytes);
std::wstring FormatFileTimeText(const FILETIME& utc_filetime);
std::wstring FormatZoomPercentText(float scale);

bool IsSupportedExtension(const std::filesystem::path& p);
bool IsWebpExtension(const std::filesystem::path& p);
bool IsGifExtension(const std::filesystem::path& p);
bool IsIcoExtension(const std::filesystem::path& p);

HICON LoadAppIcon(HINSTANCE hinstance, int width, int height);

bool TryReadMetadataUInt(IWICMetadataQueryReader* reader, const wchar_t* query, UINT* out_value);

void ClearCanvasRect(std::vector<std::uint8_t>& canvas,
                     UINT canvas_width,
                     UINT canvas_height,
                     UINT left,
                     UINT top,
                     UINT width,
                     UINT height);

void CompositeFrame(std::vector<std::uint8_t>& canvas,
                    UINT canvas_width,
                    UINT canvas_height,
                    const std::vector<std::uint8_t>& frame,
                    UINT frame_width,
                    UINT frame_height,
                    UINT offset_x,
                    UINT offset_y);

std::optional<std::filesystem::path> FindFirstSupportedImageInDirectory(const std::filesystem::path& directory);

inline constexpr int kMinWindowOpacityPercent = 1;
inline constexpr int kMaxWindowOpacityPercent = 100;

inline constexpr int kFilmStripSortFieldFileName = 0;
inline constexpr int kFilmStripSortFieldFileSize = 1;
inline constexpr int kFilmStripSortFieldFileExtension = 2;
inline constexpr int kFilmStripSortFieldCreationTime = 3;
inline constexpr int kFilmStripSortFieldModifiedTime = 4;
inline constexpr int kMinFilmStripSortField = kFilmStripSortFieldFileName;
inline constexpr int kMaxFilmStripSortField = kFilmStripSortFieldModifiedTime;

int ClampFilmStripSortField(int field);

bool LoadUserConfig(bool* out_fit_on_switch,
                    bool* out_smooth_sampling,
                    int* out_window_opacity_percent,
                    int* out_film_strip_sort_field,
                    bool* out_film_strip_sort_descending);
bool SaveUserConfig(bool fit_on_switch,
                    bool smooth_sampling,
                    int window_opacity_percent,
                    int film_strip_sort_field,
                    bool film_strip_sort_descending);
