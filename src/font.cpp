#include "font.hpp"
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>

namespace {
stbtt_fontinfo* as_info(void* p) { return static_cast<stbtt_fontinfo*>(p); }

// UI design tokens are authored at 1x; render 12.5% larger (prior 1.5x, then −25%).
constexpr float kFontSizeScale = 1.125f;
float scaled_px(float px) { return px * kFontSizeScale; }

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char sep =
#ifdef _WIN32
        '\\';
#else
        '/';
#endif
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}
}  // namespace

bool Font::load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0);
    std::vector<uint8_t> bytes;
    bytes.resize(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(bytes.data()), n);
    if (!f) return false;
    return load_memory(bytes.data(), int(bytes.size()));
}

bool Font::load_memory(const uint8_t* data, int size) {
    ttf_.assign(data, data + size);
    auto* info = new stbtt_fontinfo();
    int offset = stbtt_GetFontOffsetForIndex(ttf_.data(), 0);
    if (offset < 0) offset = 0;
    if (!stbtt_InitFont(info, ttf_.data(), offset)) {
        delete info;
        ttf_.clear();
        return false;
    }
    delete as_info(info_);
    info_ = info;
    loaded_ = true;
    cache_.clear();
    return true;
}

const Font::Glyph& Font::glyph(int codepoint, float px) const {
    uint64_t key = (uint64_t(int(px * 10)) << 32) | uint32_t(codepoint);
    for (auto& e : cache_)
        if (e.first == key) return e.second;
    Glyph g;
    if (!loaded_) {
        cache_.push_back({key, g});
        return cache_.back().second;
    }
    auto* info = as_info(info_);
    float scale = stbtt_ScaleForPixelHeight(info, px);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(info, codepoint, &adv, &lsb);
    g.advance = int(std::ceil(adv * scale));
    if (g.w > 0 && g.h > 0) {
        g.cover.resize(size_t(g.w * g.h));
        stbtt_MakeCodepointBitmap(info, g.cover.data(), g.w, g.h, g.w, scale, scale, codepoint);
    }
    cache_.push_back({key, std::move(g)});
    if (cache_.size() > 2048) cache_.erase(cache_.begin(), cache_.begin() + 256);
    return cache_.back().second;
}

const char* Font::next_utf8(const char* s, int* cp) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) {
        *cp = c;
        return s + 1;
    }
    if ((c & 0xE0) == 0xC0) {
        *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return s + 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return s + 3;
    }
    if ((c & 0xF8) == 0xF0) {
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return s + 4;
    }
    *cp = c;
    return s + 1;
}

void Font::draw(Image& dst, int x, int y, const std::string& text, float px, uint32_t color) const {
    if (text.empty()) return;
    int ascent = height(px);
    px = scaled_px(px);
    int cx = x;
    const char* s = text.c_str();
    const char* end = s + text.size();
    auto* info = as_info(info_);
    int prev = 0;
    float scale = loaded_ ? stbtt_ScaleForPixelHeight(info, px) : 1.f;
    while (s < end) {
        int cp = 0;
        s = next_utf8(s, &cp);
        if (cp == '\n') {
            cx = x;
            y += ascent + 4;
            prev = 0;
            continue;
        }
        const Glyph& g = glyph(cp, px);
        if (loaded_ && prev) {
            cx += int(stbtt_GetCodepointKernAdvance(info, prev, cp) * scale);
        }
        for (int gy = 0; gy < g.h; ++gy) {
            for (int gx = 0; gx < g.w; ++gx) {
                uint8_t cov = g.cover[size_t(gy * g.w + gx)];
                if (!cov) continue;
                uint8_t a = uint8_t((uint16_t(cov) * ca(color)) / 255);
                dst.blend(cx + g.xoff + gx, y + ascent + g.yoff + gy, with_alpha(color, a));
            }
        }
        cx += g.advance ? g.advance : int(px * 0.5f);
        prev = cp;
    }
}

int Font::width(const std::string& text, float px) const {
    px = scaled_px(px);
    int cx = 0, maxx = 0;
    const char* s = text.c_str();
    const char* end = s + text.size();
    auto* info = as_info(info_);
    int prev = 0;
    float scale = loaded_ ? stbtt_ScaleForPixelHeight(info, px) : 1.f;
    while (s < end) {
        int cp = 0;
        s = next_utf8(s, &cp);
        if (cp == '\n') {
            maxx = std::max(maxx, cx);
            cx = 0;
            prev = 0;
            continue;
        }
        const Glyph& g = glyph(cp, px);
        if (loaded_ && prev) cx += int(stbtt_GetCodepointKernAdvance(info, prev, cp) * scale);
        cx += g.advance ? g.advance : int(px * 0.5f);
        prev = cp;
    }
    return std::max(maxx, cx);
}

int Font::height(float px) const {
    px = scaled_px(px);
    if (!loaded_) return int(px);
    auto* info = as_info(info_);
    int as, ds, lg;
    stbtt_GetFontVMetrics(info, &as, &ds, &lg);
    float scale = stbtt_ScaleForPixelHeight(info, px);
    return int(std::ceil((as - ds) * scale));
}

Font& Font::instance() {
    static Font f;
    return f;
}

bool Font::load_app_fonts(const std::string& exe_dir) {
    Font& f = instance();
    std::vector<std::string> paths = {
        join_path(exe_dir, "DejaVuSans.ttf"),
        join_path(exe_dir, "assets/DejaVuSans.ttf"),
        join_path(exe_dir, "../assets/DejaVuSans.ttf"),
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\calibri.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSText.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
#endif
    };
    for (const auto& p : paths) {
        if (f.load_file(p)) return true;
    }
    return false;
}
