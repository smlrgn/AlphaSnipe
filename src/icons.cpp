#include "icons.hpp"

static void line(Image& d, float x0, float y0, float x1, float y1, float t, uint32_t c) {
    d.stroke_line(int(x0), int(y0), int(x1), int(y1), t, c);
}

void draw_icon(Image& dst, Rect r, Icon id, uint32_t color) {
    float x = float(r.x), y = float(r.y), w = float(r.w), h = float(r.h);
    float s = std::min(w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float t = std::max(1.4f, s * 0.08f);
    auto X = [&](float u) { return x + (w - s) * 0.5f + u * s; };
    auto Y = [&](float v) { return y + (h - s) * 0.5f + v * s; };

    switch (id) {
        case Icon::Rect:
            dst.stroke_rect({int(X(0.22f)), int(Y(0.22f)), int(s * 0.56f), int(s * 0.56f)}, std::max(1, int(t)), color);
            break;
        case Icon::Freeform: {
            std::vector<Point> p = {{int(X(0.22f)), int(Y(0.55f))},
                                    {int(X(0.35f)), int(Y(0.25f))},
                                    {int(X(0.62f)), int(Y(0.30f))},
                                    {int(X(0.78f)), int(Y(0.48f))},
                                    {int(X(0.70f)), int(Y(0.75f))},
                                    {int(X(0.38f)), int(Y(0.78f))}};
            dst.stroke_polyline(p, t, color, true);
            break;
        }
        case Icon::Window:
            dst.stroke_rect({int(X(0.18f)), int(Y(0.28f)), int(s * 0.48f), int(s * 0.42f)}, std::max(1, int(t)), color);
            dst.fill_rect({int(X(0.18f)), int(Y(0.28f)), int(s * 0.48f), int(s * 0.12f)}, color);
            dst.stroke_rect({int(X(0.38f)), int(Y(0.18f)), int(s * 0.44f), int(s * 0.40f)}, std::max(1, int(t)), color);
            break;
        case Icon::Full:
            dst.stroke_rect({int(X(0.18f)), int(Y(0.22f)), int(s * 0.64f), int(s * 0.56f)}, std::max(1, int(t)), color);
            dst.fill_rect({int(X(0.30f)), int(Y(0.36f)), int(s * 0.40f), int(s * 0.28f)}, with_alpha(color, 90));
            break;
        case Icon::Close:
            line(dst, X(0.28f), Y(0.28f), X(0.72f), Y(0.72f), t, color);
            line(dst, X(0.72f), Y(0.28f), X(0.28f), Y(0.72f), t, color);
            break;
        case Icon::Pen:
            line(dst, X(0.28f), Y(0.72f), X(0.62f), Y(0.28f), t + 0.4f, color);
            dst.fill_circle(int(X(0.28f)), int(Y(0.72f)), t + 0.6f, color);
            break;
        case Icon::Highlighter:
            dst.fill_rect({int(X(0.30f)), int(Y(0.28f)), int(s * 0.40f), int(s * 0.48f)}, with_alpha(color, 90));
            dst.stroke_rect({int(X(0.30f)), int(Y(0.28f)), int(s * 0.40f), int(s * 0.48f)}, 1, color);
            break;
        case Icon::Eraser:
            dst.stroke_rect({int(X(0.26f)), int(Y(0.34f)), int(s * 0.48f), int(s * 0.32f)}, std::max(1, int(t)), color);
            dst.fill_rect({int(X(0.26f)), int(Y(0.34f)), int(s * 0.18f), int(s * 0.32f)}, color);
            break;
        case Icon::Crop:
            dst.stroke_rect({int(X(0.28f)), int(Y(0.28f)), int(s * 0.44f), int(s * 0.44f)}, std::max(1, int(t)), color);
            line(dst, X(0.18f), Y(0.28f), X(0.28f), Y(0.28f), t, color);
            line(dst, X(0.28f), Y(0.18f), X(0.28f), Y(0.28f), t, color);
            line(dst, X(0.72f), Y(0.72f), X(0.82f), Y(0.72f), t, color);
            line(dst, X(0.72f), Y(0.72f), X(0.72f), Y(0.82f), t, color);
            break;
        case Icon::Undo:
            line(dst, X(0.28f), Y(0.50f), X(0.72f), Y(0.50f), t, color);
            line(dst, X(0.28f), Y(0.50f), X(0.42f), Y(0.34f), t, color);
            line(dst, X(0.28f), Y(0.50f), X(0.42f), Y(0.66f), t, color);
            break;
        case Icon::Redo:
            line(dst, X(0.28f), Y(0.50f), X(0.72f), Y(0.50f), t, color);
            line(dst, X(0.72f), Y(0.50f), X(0.58f), Y(0.34f), t, color);
            line(dst, X(0.72f), Y(0.50f), X(0.58f), Y(0.66f), t, color);
            break;
        case Icon::Copy:
            dst.stroke_rect({int(X(0.24f)), int(Y(0.30f)), int(s * 0.38f), int(s * 0.46f)}, 1, color);
            dst.stroke_rect({int(X(0.38f)), int(Y(0.22f)), int(s * 0.38f), int(s * 0.46f)}, std::max(1, int(t)), color);
            break;
        case Icon::Save:
            dst.stroke_rect({int(X(0.24f)), int(Y(0.22f)), int(s * 0.52f), int(s * 0.56f)}, std::max(1, int(t)), color);
            dst.fill_rect({int(X(0.34f)), int(Y(0.22f)), int(s * 0.32f), int(s * 0.20f)}, color);
            break;
        case Icon::Check:
            line(dst, X(0.24f), Y(0.52f), X(0.42f), Y(0.72f), t + 0.3f, color);
            line(dst, X(0.42f), Y(0.72f), X(0.78f), Y(0.28f), t + 0.3f, color);
            break;
        case Icon::Chevron:
            line(dst, X(0.35f), Y(0.38f), X(0.50f), Y(0.58f), t, color);
            line(dst, X(0.50f), Y(0.58f), X(0.65f), Y(0.38f), t, color);
            break;
        case Icon::ZoomIn:
            dst.stroke_round_rect({int(X(0.22f)), int(Y(0.20f)), int(s * 0.44f), int(s * 0.44f)}, int(s * 0.22f),
                                  std::max(1, int(t)), color);
            line(dst, X(0.44f), Y(0.30f), X(0.44f), Y(0.52f), t, color);
            line(dst, X(0.32f), Y(0.42f), X(0.56f), Y(0.42f), t, color);
            line(dst, X(0.58f), Y(0.58f), X(0.76f), Y(0.76f), t + 0.4f, color);
            break;
        case Icon::ZoomOut:
            dst.stroke_round_rect({int(X(0.22f)), int(Y(0.20f)), int(s * 0.44f), int(s * 0.44f)}, int(s * 0.22f),
                                  std::max(1, int(t)), color);
            line(dst, X(0.32f), Y(0.42f), X(0.56f), Y(0.42f), t, color);
            line(dst, X(0.58f), Y(0.58f), X(0.76f), Y(0.76f), t + 0.4f, color);
            break;
        case Icon::New:
            line(dst, X(0.50f), Y(0.26f), X(0.50f), Y(0.74f), t + 0.4f, color);
            line(dst, X(0.26f), Y(0.50f), X(0.74f), Y(0.50f), t + 0.4f, color);
            break;
        case Icon::DragDrop: {
            dst.stroke_round_rect({int(X(0.16f)), int(Y(0.32f)), int(s * 0.44f), int(s * 0.50f)}, int(s * 0.06f),
                                  std::max(1, int(t)), color);
            line(dst, X(0.24f), Y(0.46f), X(0.48f), Y(0.46f), t, color);
            line(dst, X(0.24f), Y(0.56f), X(0.44f), Y(0.56f), t, color);
            line(dst, X(0.24f), Y(0.66f), X(0.40f), Y(0.66f), t, color);
            float ax0 = X(0.50f), ay0 = Y(0.50f), ax1 = X(0.82f), ay1 = Y(0.18f);
            line(dst, ax0, ay0, ax1, ay1, t + 0.5f, color);
            line(dst, X(0.58f), Y(0.18f), ax1, ay1, t + 0.5f, color);
            line(dst, ax1, ay1, X(0.82f), Y(0.42f), t + 0.5f, color);
            break;
        }
        case Icon::Scissors: {
            dst.fill_circle(int(X(0.32f)), int(Y(0.32f)), s * 0.10f, color);
            dst.fill_circle(int(X(0.32f)), int(Y(0.68f)), s * 0.10f, color);
            line(dst, X(0.38f), Y(0.36f), X(0.78f), Y(0.72f), t, color);
            line(dst, X(0.38f), Y(0.64f), X(0.78f), Y(0.28f), t, color);
            break;
        }
    }
    (void)cx;
    (void)cy;
}
