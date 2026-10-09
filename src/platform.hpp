#pragma once

#include "common.hpp"
#include "image.hpp"

class Platform {
public:
    virtual ~Platform() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    virtual Image capture_screen() = 0;
    virtual std::vector<MonitorInfo> monitors() = 0;
    virtual std::vector<TopWindow> windows() = 0;
    virtual void mouse_screen_pos(int& x, int& y) = 0;
    virtual Rect virtual_screen() = 0;

    virtual bool create_window(const WindowDesc& d) = 0;
    virtual void destroy_window(WinKind k) = 0;
    virtual void show_window(WinKind k, bool show) = 0;
    virtual void set_title(WinKind k, const char* title) = 0;
    virtual void set_size(WinKind k, int w, int h) = 0;
    virtual void get_size(WinKind k, int& w, int& h) = 0;
    virtual void present(WinKind k, const Image& img) = 0;
    virtual bool poll(PlatEvent& e) = 0;
    virtual void wait_ms(int ms) = 0;
    virtual uint64_t ticks_ms() = 0;

    virtual bool copy_image(const Image& img) = 0;
    // Begin a native drag of the snip (file + image). Blocks until drop or cancel.
    virtual bool drag_image(WinKind source, const Image& img) = 0;
    virtual std::string save_dialog(const std::string& suggested, const std::string& start_dir) = 0;
    virtual std::string pictures_dir() = 0;
    virtual bool write_file(const std::string& path, const uint8_t* data, size_t n) = 0;
    virtual bool make_dirs(const std::string& path) = 0;
    virtual void set_cursor(CursorType c) = 0;
    virtual void beep() = 0;
    virtual bool register_hotkey() = 0;
    virtual std::string exe_dir() = 0;
};

Platform* create_platform();
