#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

enum class ImageType {
    None,
    Broken,
    Raster,
    Svg
};

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
    D2D1_RECT_F GetImageFitViewport(const D2D1_RECT_F& viewport) const;
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
    bool OpenCurrentImageInFolder();
    bool PrintCurrentImage();
    bool CopyCurrentImageToClipboard();
    bool CopyCurrentFileToClipboard();
    bool CopyCurrentImagePathToClipboard();
    bool MoveCurrentFileToRecycleBin();
    void ResetImageTransform();
    void ApplyImageTransform(int op_m11, int op_m12, int op_m21, int op_m22);
    void RotateImageClockwise();
    void RotateImageCounterclockwise();
    void ToggleImageFlipHorizontal();
    void ToggleImageFlipVertical();
    D2D1_SIZE_F GetTransformedImageSize() const;
    D2D1_MATRIX_3X2_F GetImageTransformMatrix(const D2D1_POINT_2F& center) const;
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
    void ClearDoubleClickZoomRestore();
    void HandleImageDoubleClick(POINT client_pt);

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
    int image_transform_m11_ = 1;
    int image_transform_m12_ = 0;
    int image_transform_m21_ = 0;
    int image_transform_m22_ = 1;
    bool dblclick_restore_available_ = false;
    float dblclick_restore_scale_ = 1.0f;
    std::wstring dblclick_restore_image_norm_;

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
