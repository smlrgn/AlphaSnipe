#pragma once

#include "image.hpp"

class Font {
public:
    bool load_file(const std::string& path);
    bool load_memory(const uint8_t* data, int size);
    bool ok() const { return loaded_; }

    void draw(Image& dst, int x, int y, const std::string& text, float px, uint32_t color) const;
    int width(const std::string& text, float px) const;
    int height(float px) const;

    static Font& instance();
    static bool load_app_fonts(const std::string& exe_dir);

private:
    struct Glyph {
        int w = 0, h = 0, xoff = 0, yoff = 0, advance = 0;
        std::vector<uint8_t> cover;
    };

    const Glyph& glyph(int codepoint, float px) const;
    static const char* next_utf8(const char* s, int* cp);

    bool loaded_ = false;
    std::vector<uint8_t> ttf_;
    void* info_ = nullptr;  // stbtt_fontinfo*
    mutable std::vector<std::pair<uint64_t, Glyph>> cache_;
};
