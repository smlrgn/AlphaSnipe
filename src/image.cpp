#include "image.hpp"

#include <fstream>

extern "C" {
int stbi_write_png_to_func(void (*func)(void*, void*, int), void* context,
                           int w, int h, int comp, const void* data, int stride_in_bytes);
}

void Image::resize(int nw, int nh) {
    w = std::max(0, nw);
    h = std::max(0, nh);
    px.assign(size_t(w) * size_t(h) * 4, 0);
}

void Image::fill(uint32_t c) {
    for (int i = 0; i < w * h; ++i) {
        px[size_t(i) * 4 + 0] = cr(c);
        px[size_t(i) * 4 + 1] = cg(c);
        px[size_t(i) * 4 + 2] = cb(c);
        px[size_t(i) * 4 + 3] = ca(c);
    }
}

uint32_t Image::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    const uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
    return rgba(p[0], p[1], p[2], p[3]);
}

void Image::set(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
    p[0] = cr(c);
    p[1] = cg(c);
    p[2] = cb(c);
    p[3] = ca(c);
}

void Image::blend(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t sa = ca(c);
    if (sa == 0) return;
    uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
    if (sa == 255) {
        p[0] = cr(c);
        p[1] = cg(c);
        p[2] = cb(c);
        p[3] = 255;
        return;
    }
    int inv = 255 - sa;
    p[0] = uint8_t((cr(c) * sa + p[0] * inv) / 255);
    p[1] = uint8_t((cg(c) * sa + p[1] * inv) / 255);
    p[2] = uint8_t((cb(c) * sa + p[2] * inv) / 255);
    p[3] = uint8_t(sa + p[3] * inv / 255);
}

Image Image::crop(int x, int y, int cw, int ch) const {
    if (x < 0) {
        cw += x;
        x = 0;
    }
    if (y < 0) {
        ch += y;
        y = 0;
    }
    if (x + cw > w) cw = w - x;
    if (y + ch > h) ch = h - y;
    if (cw <= 0 || ch <= 0) return {};
    Image out(cw, ch);
    for (int row = 0; row < ch; ++row) {
        std::memcpy(out.row(row), &px[(size_t(y + row) * size_t(w) + size_t(x)) * 4], size_t(cw) * 4);
    }
    return out;
}

void Image::blit(const Image& src, int dx, int dy) {
    blit(src, 0, 0, src.w, src.h, dx, dy);
}

void Image::blit(const Image& src, int sx, int sy, int sw, int sh, int dx, int dy) {
    if (!src.valid()) return;
    for (int y = 0; y < sh; ++y) {
        int syy = sy + y, dyy = dy + y;
        if (syy < 0 || syy >= src.h || dyy < 0 || dyy >= h) continue;
        for (int x = 0; x < sw; ++x) {
            int sxx = sx + x, dxx = dx + x;
            if (sxx < 0 || sxx >= src.w || dxx < 0 || dxx >= w) continue;
            const uint8_t* s = &src.px[(size_t(syy) * size_t(src.w) + size_t(sxx)) * 4];
            if (s[3] == 0) continue;
            blend(dxx, dyy, rgba(s[0], s[1], s[2], s[3]));
        }
    }
}

void Image::blit_scaled(const Image& src, Rect dst, Rect src_rect) {
    if (!src.valid() || dst.empty() || src_rect.empty()) return;
    for (int y = 0; y < dst.h; ++y) {
        int dy = dst.y + y;
        if (dy < 0 || dy >= h) continue;
        int sy = src_rect.y + int((int64_t(y) * src_rect.h) / dst.h);
        sy = clampi(sy, 0, src.h - 1);
        for (int x = 0; x < dst.w; ++x) {
            int dx = dst.x + x;
            if (dx < 0 || dx >= w) continue;
            int sx = src_rect.x + int((int64_t(x) * src_rect.w) / dst.w);
            sx = clampi(sx, 0, src.w - 1);
            const uint8_t* s = &src.px[(size_t(sy) * size_t(src.w) + size_t(sx)) * 4];
            if (s[3] == 0) continue;
            if (s[3] == 255) {
                uint8_t* d = &px[(size_t(dy) * size_t(w) + size_t(dx)) * 4];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            } else {
                blend(dx, dy, rgba(s[0], s[1], s[2], s[3]));
            }
        }
    }
}

void Image::blit_premul(const Image& src, int dx, int dy) {
    blit(src, dx, dy);
}

void Image::fill_rect(Rect r, uint32_t c) {
    int x0 = clampi(r.x, 0, w);
    int y0 = clampi(r.y, 0, h);
    int x1 = clampi(r.r(), 0, w);
    int y1 = clampi(r.b(), 0, h);
    uint8_t sa = ca(c);
    if (sa == 255) {
        for (int y = y0; y < y1; ++y) {
            uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x0)) * 4];
            for (int x = x0; x < x1; ++x) {
                p[0] = cr(c);
                p[1] = cg(c);
                p[2] = cb(c);
                p[3] = 255;
                p += 4;
            }
        }
    } else {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) blend(x, y, c);
    }
}

void Image::fill_rect_aa(float x, float y, float rw, float rh, uint32_t c) {
    int x0 = int(std::floor(x));
    int y0 = int(std::floor(y));
    int x1 = int(std::ceil(x + rw));
    int y1 = int(std::ceil(y + rh));
    for (int py = y0; py < y1; ++py) {
        for (int px_ = x0; px_ < x1; ++px_) {
            float cx0 = std::max(x, float(px_));
            float cy0 = std::max(y, float(py));
            float cx1 = std::min(x + rw, float(px_ + 1));
            float cy1 = std::min(y + rh, float(py + 1));
            float cov = std::max(0.f, cx1 - cx0) * std::max(0.f, cy1 - cy0);
            if (cov <= 0) continue;
            uint8_t a = uint8_t(clampf(cov * ca(c), 0.f, 255.f));
            blend(px_, py, with_alpha(c, a));
        }
    }
}

void Image::stroke_rect(Rect r, int thickness, uint32_t c) {
    if (thickness < 1) thickness = 1;
    fill_rect({r.x, r.y, r.w, thickness}, c);
    fill_rect({r.x, r.b() - thickness, r.w, thickness}, c);
    fill_rect({r.x, r.y, thickness, r.h}, c);
    fill_rect({r.r() - thickness, r.y, thickness, r.h}, c);
}

static float rr_dist(float px, float py, float cx, float cy, float hw, float hh, float rad) {
    float dx = std::abs(px - cx) - (hw - rad);
    float dy = std::abs(py - cy) - (hh - rad);
    float ox = std::max(dx, 0.f);
    float oy = std::max(dy, 0.f);
    float inside = std::min(std::max(dx, dy), 0.f);
    return std::sqrt(ox * ox + oy * oy) + inside - rad;
}

void Image::fill_round_rect(Rect r, int radius, uint32_t c) {
    if (r.empty()) return;
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    float cx = r.x + r.w * 0.5f;
    float cy = r.y + r.h * 0.5f;
    float hw = r.w * 0.5f;
    float hh = r.h * 0.5f;
    int x0 = clampi(r.x, 0, w);
    int y0 = clampi(r.y, 0, h);
    int x1 = clampi(r.r(), 0, w);
    int y1 = clampi(r.b(), 0, h);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float d = rr_dist(x + 0.5f, y + 0.5f, cx, cy, hw, hh, float(radius));
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov <= 0) continue;
            uint8_t a = uint8_t(cov * ca(c));
            blend(x, y, with_alpha(c, a));
        }
    }
}

void Image::stroke_round_rect(Rect r, int radius, int thickness, uint32_t c) {
    if (r.empty()) return;
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    float cx = r.x + r.w * 0.5f;
    float cy = r.y + r.h * 0.5f;
    float hw = r.w * 0.5f;
    float hh = r.h * 0.5f;
    float t = float(std::max(1, thickness));
    int x0 = clampi(r.x - 2, 0, w);
    int y0 = clampi(r.y - 2, 0, h);
    int x1 = clampi(r.r() + 2, 0, w);
    int y1 = clampi(r.b() + 2, 0, h);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float d = std::abs(rr_dist(x + 0.5f, y + 0.5f, cx, cy, hw, hh, float(radius))) - t * 0.5f;
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov <= 0) continue;
            blend(x, y, with_alpha(c, uint8_t(cov * ca(c))));
        }
    }
}

void Image::fill_circle(int cx, int cy, float radius, uint32_t c) {
    int r = int(std::ceil(radius + 1));
    int x0 = clampi(cx - r, 0, w);
    int y0 = clampi(cy - r, 0, h);
    int x1 = clampi(cx + r + 1, 0, w);
    int y1 = clampi(cy + r + 1, 0, h);
    float r2 = radius;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy) - r2;
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov <= 0) continue;
            blend(x, y, with_alpha(c, uint8_t(cov * ca(c))));
        }
    }
}

static float dist_seg(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0, vy = y1 - y0;
    float wx = px - x0, wy = py - y0;
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0) return std::hypot(px - x0, py - y0);
    float c2 = vx * vx + vy * vy;
    if (c2 <= c1) return std::hypot(px - x1, py - y1);
    float t = c1 / c2;
    return std::hypot(px - (x0 + t * vx), py - (y0 + t * vy));
}

void Image::stroke_line(int x0, int y0, int x1, int y1, float thickness, uint32_t c) {
    float r = std::max(0.5f, thickness * 0.5f);
    int minx = clampi(std::min(x0, x1) - int(r) - 1, 0, w);
    int miny = clampi(std::min(y0, y1) - int(r) - 1, 0, h);
    int maxx = clampi(std::max(x0, x1) + int(r) + 2, 0, w);
    int maxy = clampi(std::max(y0, y1) + int(r) + 2, 0, h);
    for (int y = miny; y < maxy; ++y) {
        for (int x = minx; x < maxx; ++x) {
            float d = dist_seg(x + 0.5f, y + 0.5f, float(x0), float(y0), float(x1), float(y1)) - r;
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov <= 0) continue;
            blend(x, y, with_alpha(c, uint8_t(cov * ca(c))));
        }
    }
}

void Image::stroke_polyline(const std::vector<Point>& pts, float thickness, uint32_t c, bool closed) {
    if (pts.size() < 2) {
        if (pts.size() == 1) fill_circle(pts[0].x, pts[0].y, thickness * 0.5f, c);
        return;
    }
    for (size_t i = 1; i < pts.size(); ++i) {
        stroke_line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, thickness, c);
    }
    if (closed && pts.size() > 2) {
        stroke_line(pts.back().x, pts.back().y, pts.front().x, pts.front().y, thickness, c);
    }
}

static bool point_in_poly(int x, int y, const std::vector<Point>& pts) {
    bool inside = false;
    size_t n = pts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        int yi = pts[i].y, yj = pts[j].y;
        int xi = pts[i].x, xj = pts[j].x;
        bool intersect = ((yi > y) != (yj > y)) &&
                         (x < xi + (int64_t)(xj - xi) * (y - yi) / (yj - yi + (yj == yi)));
        if (intersect) inside = !inside;
    }
    return inside;
}

void Image::fill_polygon(const std::vector<Point>& pts, uint32_t c) {
    if (pts.size() < 3) return;
    int minx = w, miny = h, maxx = 0, maxy = 0;
    for (auto p : pts) {
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x);
        maxy = std::max(maxy, p.y);
    }
    minx = clampi(minx, 0, w);
    miny = clampi(miny, 0, h);
    maxx = clampi(maxx + 1, 0, w);
    maxy = clampi(maxy + 1, 0, h);
    for (int y = miny; y < maxy; ++y)
        for (int x = minx; x < maxx; ++x)
            if (point_in_poly(x, y, pts)) blend(x, y, c);
}

void Image::apply_mask_outside(const std::vector<Point>& pts) {
    if (pts.size() < 3) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!point_in_poly(x, y, pts)) {
                uint8_t* p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                p[3] = 0;
            }
        }
    }
}

void Image::dim(uint32_t overlay) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) blend(x, y, overlay);
}

void Image::checkerboard(Rect r, int cell, uint32_t a, uint32_t b) {
    int x0 = clampi(r.x, 0, w);
    int y0 = clampi(r.y, 0, h);
    int x1 = clampi(r.r(), 0, w);
    int y1 = clampi(r.b(), 0, h);
    if (cell < 2) cell = 8;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t c = (((x / cell) + (y / cell)) & 1) ? a : b;
            set(x, y, c);
        }
    }
}

void Image::stamp_eraser(const Image& original, int cx, int cy, float radius) {
    int r = int(std::ceil(radius + 1));
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            if (d > radius) continue;
            uint32_t o = original.get(x, y);
            set(x, y, o);
        }
    }
}

struct PngCtx {
    std::vector<uint8_t>* out;
};
static void png_write_fn(void* ctx, void* data, int size) {
    auto* c = static_cast<PngCtx*>(ctx);
    auto* p = static_cast<uint8_t*>(data);
    c->out->insert(c->out->end(), p, p + size);
}

bool Image::encode_png(std::vector<uint8_t>& out) const {
    if (!valid()) return false;
    out.clear();
    PngCtx ctx{&out};
    int ok = stbi_write_png_to_func(png_write_fn, &ctx, w, h, 4, px.data(), w * 4);
    return ok != 0 && out.size() > 8;
}

bool Image::save_png(const std::string& path) const {
    std::vector<uint8_t> buf;
    if (!encode_png(buf)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    return bool(f);
}

Image Image::make_demo(int w, int h) {
    Image img(w, h);
    // Desktop-like wallpaper
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float t = float(y) / float(std::max(1, h - 1));
            uint8_t r = uint8_t(20 + 30 * t);
            uint8_t g = uint8_t(60 + 80 * (1.f - t));
            uint8_t b = uint8_t(120 + 80 * t);
            img.set(x, y, rgba(r, g, b));
        }
    }
    // Fake windows
    auto window = [&](Rect r, uint32_t title_bar, const char*) {
        img.fill_round_rect(r, 8, rgba(32, 32, 32));
        img.fill_rect({r.x, r.y, r.w, 28}, title_bar);
        img.fill_rect(r.inset(0).intersect({r.x, r.y + 28, r.w, r.h - 28}), rgba(250, 250, 250));
        img.stroke_round_rect(r, 8, 1, rgba(0, 0, 0, 80));
        // traffic dots
        img.fill_circle(r.x + 14, r.y + 14, 5, rgba(255, 95, 87));
        img.fill_circle(r.x + 32, r.y + 14, 5, rgba(255, 189, 46));
        img.fill_circle(r.x + 50, r.y + 14, 5, rgba(39, 201, 63));
    };
    window({int(w * 0.08), int(h * 0.12), int(w * 0.42), int(h * 0.48)}, rgba(0, 120, 212), "Docs");
    window({int(w * 0.38), int(h * 0.28), int(w * 0.50), int(h * 0.52)}, rgba(16, 124, 16), "Editor");
    // taskbar
    img.fill_rect({0, h - 40, w, 40}, rgba(20, 20, 20, 230));
    img.fill_round_rect({w / 2 - 180, h - 34, 360, 28}, 6, rgba(50, 50, 50));
    return img;
}

Image composite(const Image& base, const Image& ink) {
    Image out = base;
    if (!ink.valid() || ink.w != base.w || ink.h != base.h) return out;
    out.blit(ink, 0, 0);
    return out;
}

void raster_stroke(Image& ink, const std::vector<Point>& pts, float width, uint32_t color, bool eraser) {
    if (eraser) {
        // Punch alpha along the stroke.
        uint32_t clear = rgba(0, 0, 0, 0);
        if (pts.empty()) return;
        auto punch = [&](int cx, int cy) {
            float r = width * 0.5f;
            int ir = int(std::ceil(r + 1));
            for (int y = cy - ir; y <= cy + ir; ++y) {
                for (int x = cx - ir; x <= cx + ir; ++x) {
                    if (std::hypot(x + 0.5f - cx, y + 0.5f - cy) <= r) ink.set(x, y, clear);
                }
            }
        };
        punch(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) {
            int x0 = pts[i - 1].x, y0 = pts[i - 1].y, x1 = pts[i].x, y1 = pts[i].y;
            int steps = std::max(1, int(std::hypot(x1 - x0, y1 - y0)));
            for (int s = 0; s <= steps; ++s) {
                punch(x0 + (x1 - x0) * s / steps, y0 + (y1 - y0) * s / steps);
            }
        }
        return;
    }
    ink.stroke_polyline(pts, width, color, false);
}
