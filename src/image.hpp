#pragma once

#include "common.hpp"

struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;  // RGBA

    Image() = default;
    Image(int nw, int nh) { resize(nw, nh); }

    void resize(int nw, int nh);
    void fill(uint32_t c);
    bool valid() const { return w > 0 && h > 0 && int(px.size()) >= w * h * 4; }

    uint8_t* row(int y) { return &px[size_t(y) * size_t(w) * 4]; }
    const uint8_t* row(int y) const { return &px[size_t(y) * size_t(w) * 4]; }

    uint32_t get(int x, int y) const;
    void set(int x, int y, uint32_t c);
    void blend(int x, int y, uint32_t c);

    Image crop(int x, int y, int cw, int ch) const;
    void blit(const Image& src, int dx, int dy);
    void blit(const Image& src, int sx, int sy, int sw, int sh, int dx, int dy);
    void blit_scaled(const Image& src, Rect dst, Rect src_rect);
    void blit_premul(const Image& src, int dx, int dy);

    void fill_rect(Rect r, uint32_t c);
    void fill_rect_aa(float x, float y, float rw, float rh, uint32_t c);
    void stroke_rect(Rect r, int thickness, uint32_t c);
    void fill_round_rect(Rect r, int radius, uint32_t c);
    void stroke_round_rect(Rect r, int radius, int thickness, uint32_t c);
    void fill_circle(int cx, int cy, float radius, uint32_t c);
    void stroke_line(int x0, int y0, int x1, int y1, float thickness, uint32_t c);
    void stroke_polyline(const std::vector<Point>& pts, float thickness, uint32_t c, bool closed = false);
    void fill_polygon(const std::vector<Point>& pts, uint32_t c);
    void apply_mask_outside(const std::vector<Point>& pts);  // set alpha 0 outside polygon
    void dim(uint32_t overlay);
    void checkerboard(Rect r, int cell, uint32_t a, uint32_t b);
    void stamp_eraser(const Image& original, int cx, int cy, float radius);

    bool encode_png(std::vector<uint8_t>& out) const;
    bool save_png(const std::string& path) const;

    static Image make_demo(int w, int h);
};

Image composite(const Image& base, const Image& ink);
void raster_stroke(Image& ink, const std::vector<Point>& pts, float width, uint32_t color, bool eraser);
