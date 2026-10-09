#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
inline uint8_t cr(uint32_t c) { return uint8_t((c >> 16) & 255); }
inline uint8_t cg(uint32_t c) { return uint8_t((c >> 8) & 255); }
inline uint8_t cb(uint32_t c) { return uint8_t(c & 255); }
inline uint8_t ca(uint32_t c) { return uint8_t((c >> 24) & 255); }

inline uint32_t with_alpha(uint32_t c, uint8_t a) {
    return (c & 0x00FFFFFFu) | (uint32_t(a) << 24);
}

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int r() const { return x + w; }
    int b() const { return y + h; }
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    Rect inset(int n) const { return {x + n, y + n, std::max(0, w - 2 * n), std::max(0, h - 2 * n)}; }
    Rect intersect(Rect o) const {
        int nx = std::max(x, o.x);
        int ny = std::max(y, o.y);
        int nr = std::min(r(), o.r());
        int nb = std::min(b(), o.b());
        return {nx, ny, std::max(0, nr - nx), std::max(0, nb - ny)};
    }
    bool empty() const { return w <= 0 || h <= 0; }
};

struct Point {
    int x = 0, y = 0;
};

enum class SnipMode { Rectangle, Freeform, Window, Fullscreen };

inline const char* snip_mode_name(SnipMode m) {
    switch (m) {
        case SnipMode::Rectangle: return "Rectangle";
        case SnipMode::Freeform: return "Freeform";
        case SnipMode::Window: return "Window";
        case SnipMode::Fullscreen: return "Fullscreen";
    }
    return "Rectangle";
}

enum class Tool { Pen, Highlighter, Eraser, Crop, DragDrop };

enum class WinKind { Main, Overlay, Countdown, Dialog };

enum class CursorType { Arrow, Cross, Hand, IBeam, SizeAll, SizeNwse, SizeNesw, SizeWe, SizeNs, Hidden };

enum class KeyId {
    Unknown = 0,
    Esc, Return, Space, Backspace, Delete, Tab,
    Left, Right, Up, Down, Home, End,
    A, C, D, E, H, N, O, P, S, V, X, Y, Z,
    Num1, Num2, Num3, Num4,
    F1
};

struct Mods {
    static constexpr int Shift = 1;
    static constexpr int Ctrl = 2;
    static constexpr int Alt = 4;
    static constexpr int Meta = 8;
};

struct PlatEvent {
    enum Type {
        None, Close, Resize, MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Char, Hotkey, Focus
    } type = None;
    WinKind win = WinKind::Main;
    int x = 0, y = 0, dx = 0, dy = 0;
    int button = 0;  // 1 left, 2 middle, 3 right
    KeyId key = KeyId::Unknown;
    int mods = 0;
    int width = 0, height = 0;
    char32_t ch = 0;
};

struct MonitorInfo {
    int x = 0, y = 0, w = 0, h = 0;
    bool primary = false;
};

struct TopWindow {
    int x = 0, y = 0, w = 0, h = 0;
    std::string title;
    uintptr_t id = 0;
};

struct WindowDesc {
    WinKind kind = WinKind::Main;
    const char* title = "AlphaSnipe";
    int x = 0, y = 0, w = 800, h = 600;
    bool borderless = false;
    bool always_on_top = false;
    bool resizable = true;
    bool visible = true;
};

// Visual language (Windows 11-inspired dark UI)
namespace theme {
constexpr uint32_t bg            = 0xFF1F1F1Fu;
constexpr uint32_t bg_toolbar    = 0xFF2C2C2Cu;
constexpr uint32_t bg_elevated   = 0xFF323232u;
constexpr uint32_t bg_button     = 0xFF3B3B3Bu;
constexpr uint32_t bg_button_hot = 0xFF4A4A4Au;
constexpr uint32_t bg_selected   = 0xFF0F3A5Fu;
constexpr uint32_t accent        = 0xFF0078D4u;
constexpr uint32_t accent_hot    = 0xFF1A86D9u;
constexpr uint32_t text          = 0xFFF3F3F3u;
constexpr uint32_t text_dim      = 0xFF9A9A9Au;
constexpr uint32_t text_dark     = 0xFF1A1A1Au;
constexpr uint32_t danger        = 0xFFE81123u;
constexpr uint32_t success       = 0xFF0F7B0Fu;
constexpr uint32_t selection     = 0xFF4CC2FFu;
constexpr uint32_t overlay_dim   = 0x99000000u;
constexpr uint32_t white         = 0xFFFFFFFFu;
constexpr uint32_t black         = 0xFF000000u;
constexpr uint32_t stroke        = 0xFF5A5A5Au;
}

inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

inline std::string utf8_from_u32(char32_t ch) {
    std::string s;
    if (ch < 0x80) s.push_back(char(ch));
    else if (ch < 0x800) {
        s.push_back(char(0xC0 | (ch >> 6)));
        s.push_back(char(0x80 | (ch & 0x3F)));
    } else if (ch < 0x10000) {
        s.push_back(char(0xE0 | (ch >> 12)));
        s.push_back(char(0x80 | ((ch >> 6) & 0x3F)));
        s.push_back(char(0x80 | (ch & 0x3F)));
    } else {
        s.push_back(char(0xF0 | (ch >> 18)));
        s.push_back(char(0x80 | ((ch >> 12) & 0x3F)));
        s.push_back(char(0x80 | ((ch >> 6) & 0x3F)));
        s.push_back(char(0x80 | (ch & 0x3F)));
    }
    return s;
}
