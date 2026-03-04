#include "qmi_app.h"

#include "qmi_utils.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kViewportMargin = 0.0f;
constexpr float kViewportBottomGap = 0.0f;
constexpr int kTitleButtonWidth = 46;
constexpr int kTitleButtonHeight = 34;

template <typename T>
T Clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(v, hi));
}

bool IsRenderableImageTypeForView(ImageType type) {
    return type == ImageType::Raster || type == ImageType::Svg;
}
}  // namespace

void QmiApp::ResetView() {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    ClearDoubleClickZoomRestore();
}

void QmiApp::ResetImageTransform() {
    image_transform_m11_ = 1;
    image_transform_m12_ = 0;
    image_transform_m21_ = 0;
    image_transform_m22_ = 1;
}

void QmiApp::ApplyImageTransform(int op_m11, int op_m12, int op_m21, int op_m22) {
    const int m11 = image_transform_m11_ * op_m11 + image_transform_m12_ * op_m21;
    const int m12 = image_transform_m11_ * op_m12 + image_transform_m12_ * op_m22;
    const int m21 = image_transform_m21_ * op_m11 + image_transform_m22_ * op_m21;
    const int m22 = image_transform_m21_ * op_m12 + image_transform_m22_ * op_m22;
    image_transform_m11_ = m11;
    image_transform_m12_ = m12;
    image_transform_m21_ = m21;
    image_transform_m22_ = m22;
    ClearDoubleClickZoomRestore();
}

void QmiApp::RotateImageClockwise() {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }
    ApplyImageTransform(0, 1, -1, 0);
    RequestRender();
}

void QmiApp::RotateImageCounterclockwise() {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }
    ApplyImageTransform(0, -1, 1, 0);
    RequestRender();
}

void QmiApp::ToggleImageFlipHorizontal() {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }
    ApplyImageTransform(-1, 0, 0, 1);
    RequestRender();
}

void QmiApp::ToggleImageFlipVertical() {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }
    ApplyImageTransform(1, 0, 0, -1);
    RequestRender();
}

D2D1_SIZE_F QmiApp::GetTransformedImageSize() const {
    const float src_w = std::max(1.0f, current_image_.width);
    const float src_h = std::max(1.0f, current_image_.height);
    const float transformed_w = std::fabs(static_cast<float>(image_transform_m11_)) * src_w +
                                std::fabs(static_cast<float>(image_transform_m21_)) * src_h;
    const float transformed_h = std::fabs(static_cast<float>(image_transform_m12_)) * src_w +
                                std::fabs(static_cast<float>(image_transform_m22_)) * src_h;
    return D2D1::SizeF(std::max(1.0f, transformed_w), std::max(1.0f, transformed_h));
}

D2D1_MATRIX_3X2_F QmiApp::GetImageTransformMatrix(const D2D1_POINT_2F& center) const {
    const float m11 = static_cast<float>(image_transform_m11_);
    const float m12 = static_cast<float>(image_transform_m12_);
    const float m21 = static_cast<float>(image_transform_m21_);
    const float m22 = static_cast<float>(image_transform_m22_);
    const float dx = center.x - (center.x * m11 + center.y * m21);
    const float dy = center.y - (center.x * m12 + center.y * m22);
    return D2D1::Matrix3x2F(m11, m12, m21, m22, dx, dy);
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

D2D1_RECT_F QmiApp::GetImageFitViewport(const D2D1_RECT_F& viewport) const {
    const float min_fit_height = 40.0f;
    const float fit_top = std::min(viewport.top + static_cast<float>(kTitleButtonHeight), viewport.bottom - min_fit_height);
    return D2D1::RectF(viewport.left, fit_top, viewport.right, viewport.bottom);
}

float QmiApp::GetBaseImageScale(const D2D1_RECT_F& viewport) const {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return 1.0f;
    }

    const D2D1_RECT_F fit_viewport = GetImageFitViewport(viewport);
    const D2D1_SIZE_F transformed_size = GetTransformedImageSize();
    const float img_w = transformed_size.width;
    const float img_h = transformed_size.height;
    const float view_w = std::max(1.0f, fit_viewport.right - fit_viewport.left);
    const float view_h = std::max(1.0f, fit_viewport.bottom - fit_viewport.top);
    const float fit = std::min(view_w / img_w, view_h / img_h);
    return std::min(1.0f, fit);
}

D2D1_RECT_F QmiApp::GetImageDestinationRect(const D2D1_RECT_F& viewport) const {
    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const D2D1_SIZE_F transformed_size = GetTransformedImageSize();
    const float img_w = transformed_size.width;
    const float img_h = transformed_size.height;
    const float scale = std::max(0.02f, GetBaseImageScale(viewport) * zoom_);
    const float dest_w = img_w * scale;
    const float dest_h = img_h * scale;
    const D2D1_RECT_F fit_viewport = GetImageFitViewport(viewport);

    const float cx = (fit_viewport.left + fit_viewport.right) * 0.5f + pan_x_;
    const float cy = (fit_viewport.top + fit_viewport.bottom) * 0.5f + pan_y_;
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
    if (!IsRenderableImageTypeForView(current_image_.type)) {
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

    if (!IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }

    const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
    if (client_pt.x < viewport.left || client_pt.x > viewport.right || client_pt.y < viewport.top || client_pt.y > viewport.bottom) {
        return;
    }

    const D2D1_SIZE_F transformed_size = GetTransformedImageSize();
    const float img_w = transformed_size.width;
    const float img_h = transformed_size.height;
    const float base_scale = GetBaseImageScale(viewport);
    const float old_scale = std::max(0.02f, base_scale * zoom_);
    const D2D1_RECT_F fit_viewport = GetImageFitViewport(viewport);

    const float center_x = (fit_viewport.left + fit_viewport.right) * 0.5f;
    const float center_y = (fit_viewport.top + fit_viewport.bottom) * 0.5f;
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

void QmiApp::ClearDoubleClickZoomRestore() {
    dblclick_restore_available_ = false;
    dblclick_restore_scale_ = 1.0f;
    dblclick_restore_image_norm_.clear();
}

void QmiApp::HandleImageDoubleClick(POINT client_pt) {
    if (!d2d_context_ || !IsRenderableImageTypeForView(current_image_.type)) {
        return;
    }

    const D2D1_SIZE_F size = d2d_context_->GetSize();
    const D2D1_RECT_F viewport = GetImageViewport(size.width, size.height);
    if (!IsPointOverVisibleImage(client_pt, viewport)) {
        return;
    }

    const float base_scale = std::max(0.0001f, GetBaseImageScale(viewport));
    const float old_scale = std::max(0.02f, base_scale * zoom_);
    const bool at_native_scale = std::fabs(old_scale - 1.0f) <= 0.01f;
    const bool image_larger_than_viewport = base_scale < 0.999f;
    const std::wstring current_image_norm = NormalizePathLower(current_image_.path);

    float target_scale = old_scale;
    if (!at_native_scale) {
        target_scale = 1.0f;
        if (image_larger_than_viewport && old_scale < 0.99f) {
            dblclick_restore_available_ = true;
            dblclick_restore_scale_ = old_scale;
            dblclick_restore_image_norm_ = current_image_norm;
        } else {
            ClearDoubleClickZoomRestore();
        }
    } else {
        if (!image_larger_than_viewport || !dblclick_restore_available_ || dblclick_restore_image_norm_ != current_image_norm) {
            return;
        }
        target_scale = std::max(0.02f, dblclick_restore_scale_);
        if (std::fabs(target_scale - 1.0f) <= 0.01f) {
            ClearDoubleClickZoomRestore();
            return;
        }
        ClearDoubleClickZoomRestore();
    }

    const D2D1_SIZE_F transformed_size = GetTransformedImageSize();
    const float img_w = transformed_size.width;
    const float img_h = transformed_size.height;
    const D2D1_RECT_F fit_viewport = GetImageFitViewport(viewport);

    const float center_x = (fit_viewport.left + fit_viewport.right) * 0.5f;
    const float center_y = (fit_viewport.top + fit_viewport.bottom) * 0.5f;
    const float image_left_before = center_x - img_w * old_scale * 0.5f + pan_x_;
    const float image_top_before = center_y - img_h * old_scale * 0.5f + pan_y_;
    const float image_space_x = (static_cast<float>(client_pt.x) - image_left_before) / old_scale;
    const float image_space_y = (static_cast<float>(client_pt.y) - image_top_before) / old_scale;

    zoom_ = Clamp(target_scale / base_scale, 0.05f, 40.0f);
    const float new_scale = std::max(0.02f, base_scale * zoom_);
    const float new_left = static_cast<float>(client_pt.x) - image_space_x * new_scale;
    const float new_top = static_cast<float>(client_pt.y) - image_space_y * new_scale;
    pan_x_ = new_left - (center_x - img_w * new_scale * 0.5f);
    pan_y_ = new_top - (center_y - img_h * new_scale * 0.5f);

    RequestRender(true);
}
