#include "app.hpp"
#include "icons.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace {

Image overlay_select(Platform* p, Image shot, SnipMode& mode);

const uint32_t kPenColors[] = {0xFF000000u, 0xFFFFFFFFu, 0xFFE81123u, 0xFFFF8C00u,
                               0xFFFFF100u, 0xFF00CC6Au, 0xFF0078D4u, 0xFFB4009Eu};
const uint32_t kHighColors[] = {0xFFFFF100u, 0xFF00FF66u, 0xFF00B7C3u, 0xFFFF8C00u, 0xFFFF6B9Du};
const int kDelayOptions[] = {0, 3, 5, 10};
const float kPenW[] = {2.5f, 5.f, 9.f};
const float kHighW[] = {12.f, 20.f, 30.f};
const float kEraseW[] = {10.f, 18.f, 28.f};
constexpr int kBarH = 56;
constexpr int kToolbarH = kBarH * 2;
constexpr int kStatusH = 32;

int font_h(float px) { return Font::instance().height(px); }
int text_y(int box_y, int box_h, float px) { return box_y + (box_h - font_h(px)) / 2; }

std::string path_join(const std::string& a, const std::string& b) {
#ifdef _WIN32
    char sep = '\\';
#else
    char sep = '/';
#endif
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}

std::string timestamp_name() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[80];
    std::strftime(buf, sizeof(buf), "Screenshot %Y-%m-%d %H%M%S.png", &lt);
    return buf;
}

bool is_blank(const Image& img) {
    if (!img.valid()) return true;
    int n = std::min(img.w * img.h, 4000);
    for (int i = 0; i < n; ++i) {
        int x = (i * 97) % img.w, y = (i * 53) % img.h;
        if ((img.get(x, y) & 0x00FFFFFFu) > 0x101010u) return false;
    }
    return true;
}

struct Stroke {
    Tool tool = Tool::Pen;
    uint32_t color = 0xFF000000u;
    float width = 5.f;
    std::vector<Point> pts;
};

struct Ui {
    int mx = 0, my = 0;
    bool down = false;
    bool clicked = false;
    bool released = false;
    bool rclicked = false;
    int wheel = 0;
    int hot = 0, active = 0;
    std::string tip;
    int tip_x = 0, tip_y = 0;
};

struct App {
    Platform* p = nullptr;
    CliOptions opt;

    SnipMode mode = SnipMode::Rectangle;
    int delay_sec = 0;
    bool auto_copy = true;
    bool auto_save = false;
    Tool tool = Tool::Pen;
    uint32_t pen_color = 0xFFE81123u;
    uint32_t high_color = 0xFFFFF100u;
    int size_idx = 1;

    Image base, ink;
    std::vector<Stroke> strokes;
    Stroke live;
    bool drawing = false;
    bool dirty = false;
    std::string last_path;
    std::string status;
    uint64_t status_until = 0;

    float zoom = 1.f, panx = 0, pany = 0;
    bool fitted = true;
    bool panning = false;
    int pan_sx = 0, pan_sy = 0;
    float pan_ox = 0, pan_oy = 0;

    Rect crop{};
    int crop_hit = -1;
    Point crop_start{};
    Rect crop_orig{};

    Ui ui;
    int main_w = 980, main_h = 680;
    Image main_fb;
    std::string menu;
    bool running = true;
    bool space = false;

    bool save_panel = false;
    std::string save_name;

    Rect drag_btn_r{};
    Point drag_origin{};
    bool drag_armed = false;

    void toast(const std::string& s, int ms = 2500) {
        status = s;
        status_until = p->ticks_ms() + uint64_t(ms);
    }

    float tool_width() const {
        if (tool == Tool::Highlighter) return kHighW[size_idx];
        if (tool == Tool::Eraser) return kEraseW[size_idx];
        return kPenW[size_idx];
    }
    uint32_t tool_color() const {
        if (tool == Tool::Highlighter) return with_alpha(high_color, 110);
        if (tool == Tool::Eraser) return 0;
        return pen_color;
    }

    void rebuild_ink() {
        if (!base.valid()) return;
        ink.resize(base.w, base.h);
        ink.fill(rgba(0, 0, 0, 0));
        for (auto& s : strokes) raster_stroke(ink, s.pts, s.width, s.color, s.tool == Tool::Eraser);
        if (drawing && !live.pts.empty()) raster_stroke(ink, live.pts, live.width, live.color, live.tool == Tool::Eraser);
    }

    Image composed() const { return composite(base, ink); }

    void set_image(Image img) {
        base = std::move(img);
        strokes.clear();
        drawing = false;
        live = {};
        rebuild_ink();
        dirty = false;
        fitted = true;
        crop = {0, 0, base.w, base.h};
        tool = Tool::Pen;
    }

    void undo() {
        if (strokes.empty()) return;
        strokes.pop_back();
        rebuild_ink();
        dirty = true;
    }
    void redo() {}  // kept simple: undo only

    bool hit(Rect r) const { return r.contains(ui.mx, ui.my); }

    bool button(Image& fb, int id, Rect r, const char* label, bool primary, bool enabled = true) {
        bool hov = enabled && hit(r);
        if (hov) {
            ui.hot = id;
            if (!label) {
            } else
                ui.tip = label;
        }
        uint32_t bg = primary ? (hov ? theme::accent_hot : theme::accent) : (hov ? theme::bg_button_hot : theme::bg_button);
        if (!enabled) bg = theme::bg_elevated;
        fb.fill_round_rect(r, 6, bg);
        if (label && label[0] && (primary || std::string(label).size() < 18)) {
            int tw = Font::instance().width(label, 15);
            uint32_t tc = primary ? theme::white : (enabled ? theme::text : theme::text_dim);
            Font::instance().draw(fb, r.x + (r.w - tw) / 2, text_y(r.y, r.h, 15), label, 15, tc);
        }
        return enabled && hov && ui.clicked;
    }

    bool icon_button(Image& fb, int id, Rect r, Icon ic, bool selected, const char* tip, bool enabled = true) {
        bool hov = enabled && hit(r);
        if (hov) {
            ui.hot = id;
            ui.tip = tip;
            ui.tip_x = r.x;
            ui.tip_y = r.b() + 6;
        }
        bool on = selected && enabled;
        uint32_t bg = on ? theme::bg_selected : (hov ? theme::bg_button_hot : 0);
        if (bg) fb.fill_round_rect(r, 6, bg);
        if (on) fb.stroke_round_rect(r, 6, 1, theme::accent);
        uint32_t col = enabled ? (on ? theme::accent : (hov ? theme::white : theme::text)) : theme::text_dim;
        draw_icon(fb, r.inset(6), ic, col);
        return enabled && hov && ui.clicked;
    }

    void draw_tooltip(Image& fb) {
        if (ui.tip.empty() || ui.down) return;
        int tw = Font::instance().width(ui.tip, 12) + 16;
        int th = font_h(12) + 10;
        int x = clampi(ui.tip_x, 8, fb.w - tw - 8);
        int y = clampi(ui.tip_y, 8, fb.h - th - 8);
        fb.fill_round_rect({x, y, tw, th}, 4, rgba(20, 20, 20, 230));
        Font::instance().draw(fb, x + 8, text_y(y, th, 12), ui.tip, 12, theme::text);
    }

    Rect canvas_rect() const { return {0, kToolbarH, main_w, main_h - kToolbarH - kStatusH}; }

    void fit_view() {
        if (!base.valid()) return;
        Rect c = canvas_rect();
        float sx = float(c.w - 24) / float(base.w);
        float sy = float(c.h - 24) / float(base.h);
        zoom = std::max(0.05f, std::min(sx, sy));
        panx = (c.w - base.w * zoom) * 0.5f;
        pany = (c.h - base.h * zoom) * 0.5f;
        fitted = true;
    }

    Point screen_to_image(int x, int y) const {
        Rect c = canvas_rect();
        float ix = (x - c.x - panx) / zoom;
        float iy = (y - c.y - pany) / zoom;
        return {int(std::floor(ix)), int(std::floor(iy))};
    }
    Rect image_to_screen(Rect r) const {
        Rect c = canvas_rect();
        return {c.x + int(r.x * zoom + panx), c.y + int(r.y * zoom + pany), std::max(1, int(r.w * zoom)),
                std::max(1, int(r.h * zoom))};
    }

    void copy_now() {
        if (!base.valid()) return;
        if (p->copy_image(composed())) toast("Copied to clipboard");
        else toast("Could not copy to clipboard");
    }

    bool save_to(const std::string& path) {
        Image out = composed();
        std::vector<uint8_t> png;
        if (!out.encode_png(png)) return false;
        auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos) p->make_dirs(path.substr(0, slash));
        if (!p->write_file(path, png.data(), png.size())) return false;
        last_path = path;
        dirty = false;
        toast("Saved " + path);
        return true;
    }

    void save_auto() {
        std::string dir = p->pictures_dir();
        p->make_dirs(dir);
        save_to(path_join(dir, timestamp_name()));
    }

    void save_as() {
        std::string dir = p->pictures_dir();
        p->make_dirs(dir);
        std::string sug = timestamp_name();
        std::string path = p->save_dialog(sug, dir);
        if (path.empty()) {
            save_panel = true;
            save_name = sug;
            return;
        }
        save_to(path);
    }

    void apply_crop() {
        if (!base.valid()) return;
        Rect r = crop.intersect({0, 0, base.w, base.h});
        if (r.w < 2 || r.h < 2) return;
        Image nb = base.crop(r.x, r.y, r.w, r.h);
        Image ni = ink.crop(r.x, r.y, r.w, r.h);
        base = std::move(nb);
        ink = std::move(ni);
        strokes.clear();  // baked
        dirty = true;
        fitted = true;
        crop = {0, 0, base.w, base.h};
        tool = Tool::Pen;
        toast("Cropped");
    }


    Rect vs_{};

    Image capture_shot() {
        if (opt.demo) return Image::make_demo(std::max(1280, vs_.w ? vs_.w : 1280), std::max(720, vs_.h ? vs_.h : 720));
        Image shot = p->capture_screen();
        if (!shot.valid() || is_blank(shot)) {
            if (opt.demo || is_blank(shot)) {
                Rect vs = p->virtual_screen();
                Image demo = Image::make_demo(std::max(640, vs.w), std::max(480, vs.h));
                toast("Screen capture was empty — using demo desktop");
                return demo;
            }
        }
        return shot;
    }

    bool countdown() {
        if (delay_sec <= 0) return true;
        WindowDesc d;
        d.kind = WinKind::Countdown;
        d.title = "Snip";
        d.w = 180;
        d.h = 180;
        d.borderless = true;
        d.always_on_top = true;
        d.resizable = false;
        auto mons = p->monitors();
        MonitorInfo m = mons.empty() ? MonitorInfo{0, 0, 800, 600, true} : mons[0];
        for (auto& mm : mons)
            if (mm.primary) m = mm;
        d.x = m.x + (m.w - d.w) / 2;
        d.y = m.y + (m.h - d.h) / 2;
        d.visible = true;
        p->create_window(d);
        uint64_t start = p->ticks_ms();
        int total = delay_sec;
        bool cancel = false;
        while (true) {
            int left = total - int((p->ticks_ms() - start) / 1000);
            if (left <= 0) break;
            PlatEvent e;
            while (p->poll(e)) {
                if (e.type == PlatEvent::KeyDown && e.key == KeyId::Esc) cancel = true;
                if (e.type == PlatEvent::Close) cancel = true;
            }
            if (cancel) break;
            Image fb(180, 180);
            fb.fill(0);
            fb.fill_round_rect({10, 10, 160, 160}, 24, rgba(20, 20, 20, 230));
            char n[16];
            std::snprintf(n, sizeof(n), "%d", left);
            int tw = Font::instance().width(n, 64);
            int nh = font_h(64);
            int ch = font_h(12);
            int ny = (180 - nh - ch - 12) / 2;
            Font::instance().draw(fb, (180 - tw) / 2, ny, n, 64, theme::white);
            int cw = Font::instance().width("Esc to cancel", 12);
            Font::instance().draw(fb, (180 - cw) / 2, ny + nh + 6, "Esc to cancel", 12, theme::text_dim);
            p->present(WinKind::Countdown, fb);
            p->wait_ms(30);
        }
        p->destroy_window(WinKind::Countdown);
        return !cancel;
    }

    void new_snip() {
        p->show_window(WinKind::Main, false);
        p->wait_ms(120);
        if (!countdown()) {
            p->show_window(WinKind::Main, true);
            return;
        }
        Image shot = capture_shot();
        if (!shot.valid()) {
            p->show_window(WinKind::Main, true);
            toast("Capture failed");
            return;
        }
        if (mode == SnipMode::Fullscreen) {
            set_image(std::move(shot));
            p->show_window(WinKind::Main, true);
            if (auto_copy) copy_now();
            if (auto_save) save_auto();
            return;
        }
        Image got = overlay_select(p, std::move(shot), mode);
        p->show_window(WinKind::Main, true);
        if (got.valid()) {
            set_image(std::move(got));
            if (auto_copy) copy_now();
            if (auto_save) save_auto();
        }
    }

    void draw_empty(Image& fb, Rect c) {
        draw_icon(fb, {c.x + c.w / 2 - 36, c.y + c.h / 2 - 90, 72, 72}, Icon::Scissors, theme::accent);
        const char* t1 = "New snip to capture the screen";
        const char* t2 = "Ctrl+Shift+S   ·   rectangle, freeform, window, fullscreen";
        int w1 = Font::instance().width(t1, 20);
        int w2 = Font::instance().width(t2, 13);
        int y = c.y + c.h / 2 - 8;
        Font::instance().draw(fb, c.x + (c.w - w1) / 2, y, t1, 20, theme::text);
        Font::instance().draw(fb, c.x + (c.w - w2) / 2, y + font_h(20) + 8, t2, 13, theme::text_dim);
    }

    void draw_main() {
        if (main_fb.w != main_w || main_fb.h != main_h) main_fb.resize(main_w, main_h);
        Image& fb = main_fb;
        fb.fill(theme::bg);
        ui.hot = 0;
        ui.tip.clear();

        // Two-row toolbar: capture actions on row 1, annotation tools on row 2.
        fb.fill_rect({0, 0, main_w, kToolbarH}, theme::bg_toolbar);
        fb.fill_rect({0, kBarH - 1, main_w, 1}, rgba(255, 255, 255, 18));
        fb.fill_rect({0, kToolbarH - 1, main_w, 1}, rgba(255, 255, 255, 18));

        const int y1 = 10;
        const int y2 = kBarH + 10;
        const int kBtn = 36;
        bool has = base.valid();

        int x = 12;
        int new_w = std::max(92, Font::instance().width("+  New", 15) + 28);
        if (button(fb, 1, {x, y1, new_w, 36}, "+  New", true)) new_snip();
        x += new_w + 12;

        int mode_w = std::max(138, Font::instance().width(snip_mode_name(mode), 14) + 48);
        Rect mode_r{x, y1, mode_w, 36};
        fb.fill_round_rect(mode_r, 6, hit(mode_r) ? theme::bg_button_hot : theme::bg_button);
        Font::instance().draw(fb, mode_r.x + 12, text_y(mode_r.y, mode_r.h, 14), snip_mode_name(mode), 14, theme::text);
        draw_icon(fb, {mode_r.r() - 28, mode_r.y + 6, 24, 24}, Icon::Chevron, theme::text_dim);
        if (hit(mode_r) && ui.clicked) menu = (menu == "mode") ? "" : "mode";
        x += mode_w + 10;

        char dlab[32];
        if (delay_sec == 0) std::snprintf(dlab, sizeof(dlab), "No delay");
        else std::snprintf(dlab, sizeof(dlab), "Delay %ds", delay_sec);
        int delay_w = std::max(118, Font::instance().width(dlab, 14) + 48);
        Rect delay_r{x, y1, delay_w, 36};
        fb.fill_round_rect(delay_r, 6, hit(delay_r) ? theme::bg_button_hot : theme::bg_button);
        Font::instance().draw(fb, delay_r.x + 12, text_y(delay_r.y, delay_r.h, 14), dlab, 14, theme::text);
        draw_icon(fb, {delay_r.r() - 28, delay_r.y + 6, 24, 24}, Icon::Chevron, theme::text_dim);
        if (hit(delay_r) && ui.clicked) menu = (menu == "delay") ? "" : "delay";

        int save_x = main_w - 12 - kBtn;
        int drag_x = save_x - 8 - kBtn;
        int copy_x = drag_x - 16 - kBtn;
        int zoomin_x = copy_x - 8 - kBtn;
        int zoomout_x = zoomin_x - 8 - kBtn;
        if (icon_button(fb, 30, {zoomout_x, y1, kBtn, kBtn}, Icon::ZoomOut, false, "Zoom out", has)) {
            zoom = std::max(0.05f, zoom / 1.15f);
            fitted = false;
        }
        if (icon_button(fb, 31, {zoomin_x, y1, kBtn, kBtn}, Icon::ZoomIn, false, "Zoom in", has)) {
            zoom = std::min(8.f, zoom * 1.15f);
            fitted = false;
        }
        if (icon_button(fb, 32, {copy_x, y1, kBtn, kBtn}, Icon::Copy, false, "Copy (Ctrl+C)", has)) copy_now();
        int gap_l = copy_x + kBtn;
        int gap_r = drag_x;
        if (gap_r - gap_l > 8) fb.fill_rect({gap_l + (gap_r - gap_l) / 2, y1 + 8, 1, 20}, rgba(255, 255, 255, 40));
        drag_btn_r = {drag_x, y1, kBtn, kBtn};
        if (icon_button(fb, 34, drag_btn_r, Icon::DragDrop, tool == Tool::DragDrop, "Drag and drop (D)", has)) {
            tool = Tool::DragDrop;
            drag_origin = {ui.mx, ui.my};
            drag_armed = true;
            menu.clear();
        }
        if (icon_button(fb, 33, {save_x, y1, kBtn, kBtn}, Icon::Save, false, "Save (Ctrl+S)", has)) save_as();

        x = 12;
        if (icon_button(fb, 10, {x, y2, 36, 36}, Icon::Pen, tool == Tool::Pen, "Pen (P)", has)) tool = Tool::Pen;
        x += 40;
        if (icon_button(fb, 11, {x, y2, 36, 36}, Icon::Highlighter, tool == Tool::Highlighter, "Highlighter (H)", has))
            tool = Tool::Highlighter;
        x += 40;
        if (icon_button(fb, 12, {x, y2, 36, 36}, Icon::Eraser, tool == Tool::Eraser, "Eraser (E)", has)) tool = Tool::Eraser;
        x += 40;
        if (icon_button(fb, 13, {x, y2, 36, 36}, Icon::Crop, tool == Tool::Crop, "Crop", has)) {
            tool = Tool::Crop;
            crop = {0, 0, base.w, base.h};
        }
        x += 48;

        if (has && (tool == Tool::Pen || tool == Tool::Highlighter)) {
            const uint32_t* cols = tool == Tool::Pen ? kPenColors : kHighColors;
            int n = tool == Tool::Pen ? 8 : 5;
            for (int i = 0; i < n; ++i) {
                Rect cr{x + i * 22, y2 + 8, 18, 18};
                uint32_t col = cols[i];
                fb.fill_circle(cr.x + 9, cr.y + 9, 8, col);
                bool sel = tool == Tool::Pen ? (pen_color == col) : (high_color == col);
                if (sel) fb.stroke_round_rect({cr.x - 2, cr.y - 2, 22, 22}, 11, 2, theme::white);
                if (hit(cr) && ui.clicked) {
                    if (tool == Tool::Pen) pen_color = col;
                    else high_color = col;
                }
            }
            x += n * 22 + 8;
            const char* labs[3] = {"S", "M", "L"};
            int chip_w = std::max(24, Font::instance().width("M", 13) + 12);
            int chip_h = std::max(28, font_h(13) + 8);
            for (int i = 0; i < 3; ++i) {
                Rect sr{x + i * (chip_w + 2), y2 + (36 - chip_h) / 2, chip_w, chip_h};
                bool sel = size_idx == i;
                fb.fill_round_rect(sr, 4, sel ? theme::bg_selected : (hit(sr) ? theme::bg_button_hot : theme::bg_button));
                int tw = Font::instance().width(labs[i], 13);
                Font::instance().draw(fb, sr.x + (chip_w - tw) / 2, text_y(sr.y, sr.h, 13), labs[i], 13, theme::text);
                if (hit(sr) && ui.clicked) size_idx = i;
            }
            x += 3 * (chip_w + 2) + 8;
        }

        fb.fill_rect({x, y2 + 8, 1, 20}, rgba(255, 255, 255, 24));
        x += 12;
        if (icon_button(fb, 20, {x, y2, 36, 36}, Icon::Undo, false, "Undo (Ctrl+Z)", has && !strokes.empty())) undo();

        // canvas
        Rect c = canvas_rect();
        fb.fill_rect(c, rgba(18, 18, 18));
        if (!has) {
            draw_empty(fb, c);
        } else {
            if (fitted) fit_view();
            Rect vis = image_to_screen({0, 0, base.w, base.h});
            fb.checkerboard(vis, 8, rgba(40, 40, 40), rgba(28, 28, 28));
            Image view = composed();
            fb.blit_scaled(view, vis, {0, 0, base.w, base.h});

            if (tool == Tool::DragDrop) {
                fb.stroke_round_rect(vis.inset(-3), 8, 2, theme::accent);
                int hw = std::max(108, Font::instance().width("Drag snip", 13) + 48);
                int hh = std::max(32, font_h(13) + 12);
                Rect handle{vis.x + vis.w / 2 - hw / 2, vis.y - hh - 8, hw, hh};
                if (handle.y < c.y + 6) handle.y = vis.y + 8;
                fb.fill_round_rect(handle, 8, theme::accent);
                draw_icon(fb, {handle.x + 6, handle.y + (hh - 24) / 2, 24, 24}, Icon::DragDrop, theme::white);
                Font::instance().draw(fb, handle.x + 32, text_y(handle.y, handle.h, 13), "Drag snip", 13, theme::white);
            }
            if (tool == Tool::Crop) {
                Rect sr = image_to_screen(crop);
                // dim outside
                fb.fill_rect({c.x, c.y, c.w, std::max(0, sr.y - c.y)}, rgba(0, 0, 0, 120));
                fb.fill_rect({c.x, sr.b(), c.w, std::max(0, c.b() - sr.b())}, rgba(0, 0, 0, 120));
                fb.fill_rect({c.x, sr.y, std::max(0, sr.x - c.x), sr.h}, rgba(0, 0, 0, 120));
                fb.fill_rect({sr.r(), sr.y, std::max(0, c.r() - sr.r()), sr.h}, rgba(0, 0, 0, 120));
                fb.stroke_rect(sr, 2, theme::white);
                int hs = 7;
                Point hspts[8] = {{sr.x, sr.y},         {sr.x + sr.w / 2, sr.y}, {sr.r(), sr.y},
                                  {sr.r(), sr.y + sr.h / 2}, {sr.r(), sr.b()},        {sr.x + sr.w / 2, sr.b()},
                                  {sr.x, sr.b()},       {sr.x, sr.y + sr.h / 2}};
                for (auto hp : hspts) fb.fill_rect({hp.x - hs / 2, hp.y - hs / 2, hs, hs}, theme::white);
                int aw = std::max(84, Font::instance().width("Apply", 15) + 24);
                if (button(fb, 40, {sr.x, sr.y - 36, aw, 32}, "Apply", true)) apply_crop();
            }
        }

        // status
        fb.fill_rect({0, main_h - kStatusH, main_w, kStatusH}, theme::bg_toolbar);
        std::string st;
        if (p->ticks_ms() < status_until) st = status;
        else if (has) {
            char b[128];
            std::snprintf(b, sizeof(b), "%d × %d    %d%%", base.w, base.h, int(zoom * 100 + 0.5f));
            st = b;
            if (tool == Tool::DragDrop) st = "Drag the snip onto another app  ·  Esc to cancel";
            if (dirty && tool != Tool::DragDrop) st += "    Unsaved";
            if (!last_path.empty() && tool != Tool::DragDrop) st += "    " + last_path;
        } else {
            st = auto_copy ? "Automatically copy to clipboard" : "Copy with Ctrl+C after a snip";
        }
        Font::instance().draw(fb, 12, main_h - kStatusH + (kStatusH - font_h(12)) / 2, st, 12, theme::text_dim);

        // menus
        if (menu == "mode") {
            const char* items[4] = {"Rectangle", "Freeform", "Window", "Fullscreen"};
            SnipMode ms[4] = {SnipMode::Rectangle, SnipMode::Freeform, SnipMode::Window, SnipMode::Fullscreen};
            int row_h = std::max(32, font_h(14) + 12);
            int box_w = std::max(mode_r.w, 160);
            for (int i = 0; i < 4; ++i) box_w = std::max(box_w, Font::instance().width(items[i], 14) + 28);
            Rect box{mode_r.x, kBarH - 2, box_w, 4 * row_h + 8};
            fb.fill_round_rect(box, 8, theme::bg_elevated);
            fb.stroke_round_rect(box, 8, 1, rgba(255, 255, 255, 24));
            for (int i = 0; i < 4; ++i) {
                Rect ir{box.x + 4, box.y + 4 + i * row_h, box.w - 8, row_h - 2};
                if (hit(ir)) fb.fill_round_rect(ir, 4, theme::bg_button_hot);
                Font::instance().draw(fb, ir.x + 10, text_y(ir.y, ir.h, 14), items[i], 14,
                                      mode == ms[i] ? theme::accent : theme::text);
                if (hit(ir) && ui.clicked) {
                    mode = ms[i];
                    menu.clear();
                }
            }
        }
        if (menu == "delay") {
            const char* items[4] = {"No delay", "3 seconds", "5 seconds", "10 seconds"};
            int row_h = std::max(32, font_h(14) + 12);
            int box_w = std::max(delay_r.w, 160);
            for (int i = 0; i < 4; ++i) box_w = std::max(box_w, Font::instance().width(items[i], 14) + 28);
            Rect box{delay_r.x, kBarH - 2, box_w, 4 * row_h + 8};
            fb.fill_round_rect(box, 8, theme::bg_elevated);
            fb.stroke_round_rect(box, 8, 1, rgba(255, 255, 255, 24));
            for (int i = 0; i < 4; ++i) {
                Rect ir{box.x + 4, box.y + 4 + i * row_h, box.w - 8, row_h - 2};
                if (hit(ir)) fb.fill_round_rect(ir, 4, theme::bg_button_hot);
                Font::instance().draw(fb, ir.x + 10, text_y(ir.y, ir.h, 14), items[i], 14,
                                      delay_sec == kDelayOptions[i] ? theme::accent : theme::text);
                if (hit(ir) && ui.clicked) {
                    delay_sec = kDelayOptions[i];
                    menu.clear();
                }
            }
        }

        if (save_panel) draw_save_panel(fb);

        draw_tooltip(fb);
        if (tool == Tool::DragDrop && has) p->set_cursor(CursorType::Hand);
        else p->set_cursor(CursorType::Arrow);
        p->present(WinKind::Main, fb);
    }

    void draw_save_panel(Image& fb) {
        fb.fill_rect({0, 0, main_w, main_h}, rgba(0, 0, 0, 120));
        int title_h = font_h(18);
        int label_h = font_h(12);
        int field_h = std::max(34, font_h(14) + 14);
        int box_h = 20 + title_h + 10 + label_h + 6 + field_h + 16 + 36 + 16;
        Rect box{(main_w - 440) / 2, (main_h - box_h) / 2, 440, box_h};
        fb.fill_round_rect(box, 12, theme::bg_elevated);
        fb.stroke_round_rect(box, 12, 1, rgba(255, 255, 255, 30));
        Font::instance().draw(fb, box.x + 20, box.y + 16, "Save snip", 18, theme::text);
        Font::instance().draw(fb, box.x + 20, box.y + 16 + title_h + 8, "File name", 12, theme::text_dim);
        Rect field{box.x + 20, box.y + 16 + title_h + 8 + label_h + 4, box.w - 40, field_h};
        fb.fill_round_rect(field, 6, theme::bg);
        Font::instance().draw(fb, field.x + 10, text_y(field.y, field.h, 14), save_name, 14, theme::text);
        fb.fill_rect({field.x + 10 + Font::instance().width(save_name, 14) + 1, text_y(field.y, field.h, 14), 1, font_h(14)},
                     theme::accent);
        if (button(fb, 90, {box.r() - 210, box.b() - 48, 90, 32}, "Cancel", false)) save_panel = false;
        if (button(fb, 91, {box.r() - 110, box.b() - 48, 90, 32}, "Save", true)) {
            save_to(path_join(p->pictures_dir(), save_name));
            save_panel = false;
        }
    }

    int crop_handle_at(int x, int y) {
        Rect sr = image_to_screen(crop);
        int hs = 10;
        Point hspts[8] = {{sr.x, sr.y},         {sr.x + sr.w / 2, sr.y}, {sr.r(), sr.y},
                          {sr.r(), sr.y + sr.h / 2}, {sr.r(), sr.b()},        {sr.x + sr.w / 2, sr.b()},
                          {sr.x, sr.b()},       {sr.x, sr.y + sr.h / 2}};
        for (int i = 0; i < 8; ++i) {
            if (std::abs(x - hspts[i].x) <= hs && std::abs(y - hspts[i].y) <= hs) return i;
        }
        if (sr.contains(x, y)) return 8;
        return -1;
    }

    void handle_canvas() {
        if (!base.valid()) return;
        Rect c = canvas_rect();
        if (!c.contains(ui.mx, ui.my) && !drawing && crop_hit < 0 && !panning) return;

        if (ui.wheel && c.contains(ui.mx, ui.my)) {
            Point before = screen_to_image(ui.mx, ui.my);
            float f = ui.wheel > 0 ? 1.15f : 1.f / 1.15f;
            zoom = clampf(zoom * f, 0.05f, 8.f);
            fitted = false;
            Point after = screen_to_image(ui.mx, ui.my);
            panx += (after.x - before.x) * zoom;
            pany += (after.y - before.y) * zoom;
        }

        if ((space || ui.rclicked) && ui.clicked && c.contains(ui.mx, ui.my)) {
            panning = true;
            pan_sx = ui.mx;
            pan_sy = ui.my;
            pan_ox = panx;
            pan_oy = pany;
            fitted = false;
        }
        if (panning) {
            panx = pan_ox + (ui.mx - pan_sx);
            pany = pan_oy + (ui.my - pan_sy);
            if (ui.released) panning = false;
            return;
        }

        Point ip = screen_to_image(ui.mx, ui.my);

        if (tool == Tool::DragDrop) {
            Rect vis = image_to_screen({0, 0, base.w, base.h});
            if (ui.clicked && (vis.contains(ui.mx, ui.my) || drag_btn_r.contains(ui.mx, ui.my))) {
                drag_origin = {ui.mx, ui.my};
                drag_armed = true;
            }
            return;
        }

        if (tool == Tool::Crop) {
            if (ui.clicked) {
                crop_hit = crop_handle_at(ui.mx, ui.my);
                crop_start = ip;
                crop_orig = crop;
            }
            if (crop_hit >= 0 && ui.down) {
                int dx = ip.x - crop_start.x;
                int dy = ip.y - crop_start.y;
                Rect r = crop_orig;
                auto apply_h = [&](int h) {
                    if (h == 0 || h == 6 || h == 7) r.x += dx, r.w -= dx;
                    if (h == 2 || h == 3 || h == 4) r.w += dx;
                    if (h == 0 || h == 1 || h == 2) r.y += dy, r.h -= dy;
                    if (h == 4 || h == 5 || h == 6) r.h += dy;
                    if (h == 8) r.x += dx, r.y += dy;
                };
                apply_h(crop_hit);
                if (r.w < 2) r.w = 2;
                if (r.h < 2) r.h = 2;
                crop = r.intersect({0, 0, base.w, base.h});
            }
            if (ui.released) crop_hit = -1;
            return;
        }

        if (ui.clicked && c.contains(ui.mx, ui.my) && !menu.empty()) return;
        if (ui.clicked && c.contains(ui.mx, ui.my)) {
            drawing = true;
            live.tool = tool;
            live.color = tool_color();
            live.width = tool_width();
            live.pts = {ip};
        }
        if (drawing && ui.down) {
            if (live.pts.empty() || live.pts.back().x != ip.x || live.pts.back().y != ip.y) live.pts.push_back(ip);
            rebuild_ink();
        }
        if (drawing && ui.released) {
            if (live.pts.size() >= 1) {
                strokes.push_back(live);
                dirty = true;
            }
            live = {};
            drawing = false;
            rebuild_ink();
        }
    }

    void handle_event(const PlatEvent& e) {
        if (e.win != WinKind::Main && e.type != PlatEvent::Hotkey) return;
        if (e.type == PlatEvent::Close) running = false;
        if (e.type == PlatEvent::Resize) {
            main_w = std::max(640, e.width);
            main_h = std::max(420, e.height);
            fitted = true;
        }
        if (e.type == PlatEvent::MouseMove) {
            ui.mx = e.x;
            ui.my = e.y;
        }
        if (e.type == PlatEvent::MouseDown) {
            ui.mx = e.x;
            ui.my = e.y;
            if (e.button == 1) {
                ui.down = true;
                ui.clicked = true;
            }
            if (e.button == 2 || e.button == 3) ui.rclicked = true;
            if (!menu.empty()) {
                // close menu if click outside; menus handled in draw via clicked
            }
        }
        if (e.type == PlatEvent::MouseUp) {
            ui.mx = e.x;
            ui.my = e.y;
            if (e.button == 1) {
                ui.down = false;
                ui.released = true;
            }
        }
        if (e.type == PlatEvent::Wheel) ui.wheel = e.dy;
        if (e.type == PlatEvent::Hotkey) new_snip();
        if (e.type == PlatEvent::KeyDown) {
            if (e.key == KeyId::Space) space = true;
            if (save_panel) {
                if (e.key == KeyId::Esc) save_panel = false;
                if (e.key == KeyId::Return) {
                    save_to(path_join(p->pictures_dir(), save_name));
                    save_panel = false;
                }
                if (e.key == KeyId::Backspace && !save_name.empty()) save_name.pop_back();
                if (e.ch >= 32 && e.ch < 127) save_name += char(e.ch);
                return;
            }
            bool ctrl = e.mods & Mods::Ctrl;
            if (e.key == KeyId::Esc) {
                if (tool == Tool::DragDrop) {
                    tool = Tool::Pen;
                    drag_armed = false;
                } else if (tool == Tool::Crop) {
                    tool = Tool::Pen;
                }
                menu.clear();
            }
            if (ctrl && e.key == KeyId::N) new_snip();
            if (ctrl && e.key == KeyId::C) copy_now();
            if (ctrl && e.key == KeyId::S) {
                if (e.mods & Mods::Shift) save_as();
                else if (!last_path.empty()) save_to(last_path);
                else save_as();
            }
            if (ctrl && e.key == KeyId::Z) undo();
            if (!ctrl && e.key == KeyId::P) tool = Tool::Pen;
            if (!ctrl && e.key == KeyId::H) tool = Tool::Highlighter;
            if (!ctrl && e.key == KeyId::E) tool = Tool::Eraser;
            if (!ctrl && e.key == KeyId::D && base.valid()) {
                tool = Tool::DragDrop;
                drag_armed = false;
            }
            if (e.key == KeyId::Return && tool == Tool::Crop) apply_crop();
        }
        if (e.type == PlatEvent::Char && save_panel && e.ch >= 32) {
            if (!(e.ch == 's' || e.ch == 'S')) { /* already handled in keydown for ascii */
            }
        }
        if (e.type == PlatEvent::KeyUp && e.key == KeyId::Space) space = false;
    }

    int run() {
        vs_ = p->virtual_screen();
        Font::load_app_fonts(p->exe_dir());

        WindowDesc d;
        d.kind = WinKind::Main;
        d.title = "AlphaSnipe";
        d.w = main_w;
        d.h = main_h;
        d.resizable = true;
        d.visible = true;
        // center
        auto mons = p->monitors();
        MonitorInfo m = mons.empty() ? MonitorInfo{0, 0, 1280, 800, true} : mons[0];
        for (auto& mm : mons)
            if (mm.primary) m = mm;
        d.x = m.x + (m.w - main_w) / 2;
        d.y = m.y + (m.h - main_h) / 2;
        if (!p->create_window(d)) return 1;
        p->get_size(WinKind::Main, main_w, main_h);
        p->register_hotkey();

        if (opt.start_new) new_snip();

        bool need_draw = true;
        while (running) {
            ui.clicked = false;
            ui.released = false;
            ui.rclicked = false;
            ui.wheel = 0;
            PlatEvent e;
            bool any = false;
            while (p->poll(e)) {
                handle_event(e);
                any = true;
                need_draw = true;
            }
            bool was_drawing = drawing || panning || crop_hit >= 0;
            handle_canvas();
            if (drawing || panning || crop_hit >= 0 || was_drawing) need_draw = true;
            if (status_until && p->ticks_ms() > status_until) {
                status_until = 0;
                need_draw = true;
            }
            if (need_draw) {
                draw_main();
                need_draw = false;
            }
            if (tool == Tool::DragDrop && drag_armed && ui.down && base.valid()) {
                float dist = std::hypot(float(ui.mx - drag_origin.x), float(ui.my - drag_origin.y));
                if (dist >= 6.f) {
                    drag_armed = false;
                    bool ok = p->drag_image(WinKind::Main, composed());
                    toast(ok ? "Dropped snip" : "Drag cancelled");
                    ui.down = false;
                    ui.released = false;
                    need_draw = true;
                }
            }
            if (ui.released) drag_armed = false;
            p->wait_ms(any || ui.down || drawing || drag_armed ? 10 : 33);
        }
        p->destroy_window(WinKind::Main);
        return 0;
    }
};

Rect rect_from_points(Point a, Point b, int maxw, int maxh) {
    Rect r{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
    return r.intersect({0, 0, maxw, maxh});
}

void sel_handle_points(Rect sr, Point out[8]) {
    out[0] = {sr.x, sr.y};
    out[1] = {sr.x + sr.w / 2, sr.y};
    out[2] = {sr.r(), sr.y};
    out[3] = {sr.r(), sr.y + sr.h / 2};
    out[4] = {sr.r(), sr.b()};
    out[5] = {sr.x + sr.w / 2, sr.b()};
    out[6] = {sr.x, sr.b()};
    out[7] = {sr.x, sr.y + sr.h / 2};
}

int sel_handle_at(Rect sr, int x, int y) {
    Point hspts[8];
    sel_handle_points(sr, hspts);
    const int hs = 10;
    for (int i = 0; i < 8; ++i) {
        if (std::abs(x - hspts[i].x) <= hs && std::abs(y - hspts[i].y) <= hs) return i;
    }
    if (sr.contains(x, y)) return 8;
    return -1;
}

Rect apply_sel_handle(Rect r, int h, int dx, int dy, int maxw, int maxh) {
    if (h == 0 || h == 6 || h == 7) {
        r.x += dx;
        r.w -= dx;
    }
    if (h == 2 || h == 3 || h == 4) r.w += dx;
    if (h == 0 || h == 1 || h == 2) {
        r.y += dy;
        r.h -= dy;
    }
    if (h == 4 || h == 5 || h == 6) r.h += dy;
    if (r.w < 8) {
        if (h == 0 || h == 6 || h == 7) r.x -= 8 - r.w;
        r.w = 8;
    }
    if (r.h < 8) {
        if (h == 0 || h == 1 || h == 2) r.y -= 8 - r.h;
        r.h = 8;
    }
    return r.intersect({0, 0, maxw, maxh});
}

Image overlay_select(Platform* p, Image shot, SnipMode& mode) {
    Rect vs = p->virtual_screen();
    if (!shot.valid()) return {};
    if (shot.w != vs.w || shot.h != vs.h) vs = {0, 0, shot.w, shot.h};

    Image dim = shot;
    dim.dim(theme::overlay_dim);
    auto tops = p->windows();
    auto mons = p->monitors();

    WindowDesc d;
    d.kind = WinKind::Overlay;
    d.title = "Snip";
    d.x = vs.x;
    d.y = vs.y;
    d.w = vs.w;
    d.h = vs.h;
    d.borderless = true;
    d.always_on_top = true;
    d.resizable = false;
    d.visible = true;
    if (!p->create_window(d)) return {};
    p->set_cursor(CursorType::Cross);

    int mx = vs.w / 2, my = 40;
    bool down = false, just_up = false, just_down = false;
    Point a{}, b{}, down_pt{};
    std::vector<Point> lasso;
    int hover = -1;
    SnipMode omode = mode;
    Image fb(vs.w, vs.h);
    Image result;
    bool done = false, cancel = false;
    bool overlay_dirty = true;

    Rect sel{};
    bool has_sel = false;
    bool drawing_rect = false;

    auto tb_mon = [&]() {
        for (auto& m : mons) {
            if (Rect{m.x - vs.x, m.y - vs.y, m.w, m.h}.contains(mx, my)) return m;
        }
        for (auto& m : mons)
            if (m.primary) return m;
        return mons.empty() ? MonitorInfo{vs.x, vs.y, vs.w, vs.h, true} : mons[0];
    };

    auto commit_rect = [&]() {
        Rect r = sel.intersect({0, 0, shot.w, shot.h});
        if (r.w < 8 || r.h < 8) return false;
        result = shot.crop(r.x, r.y, r.w, r.h);
        done = true;
        return true;
    };

    auto clear_rect_sel = [&]() {
        has_sel = false;
        sel = {};
        drawing_rect = false;
    };

    while (!done && !cancel) {
        just_up = just_down = false;
        PlatEvent e;
        while (p->poll(e)) {
            overlay_dirty = true;
            if (e.type == PlatEvent::Close) cancel = true;
            if (e.type == PlatEvent::KeyDown) {
                if (e.key == KeyId::Esc) cancel = true;
                if (e.key == KeyId::Num1) omode = SnipMode::Rectangle;
                if (e.key == KeyId::Num2) {
                    omode = SnipMode::Freeform;
                    clear_rect_sel();
                }
                if (e.key == KeyId::Num3) {
                    omode = SnipMode::Window;
                    clear_rect_sel();
                }
                if (e.key == KeyId::Num4) omode = SnipMode::Fullscreen;
            }
            if (e.type == PlatEvent::MouseMove || e.type == PlatEvent::MouseDown || e.type == PlatEvent::MouseUp) {
                mx = e.x;
                my = e.y;
            }
            if (e.type == PlatEvent::MouseDown && e.button == 1) {
                down = true;
                just_down = true;
                down_pt = {mx, my};
                if (omode != SnipMode::Rectangle) {
                    a = b = {mx, my};
                    lasso.clear();
                    lasso.push_back(a);
                }
            }
            if (e.type == PlatEvent::MouseMove && down) {
                b = {mx, my};
                if (omode == SnipMode::Freeform) {
                    if (lasso.empty() || std::hypot(lasso.back().x - mx, lasso.back().y - my) >= 1.5)
                        lasso.push_back({mx, my});
                }
            }
            if (e.type == PlatEvent::MouseUp && e.button == 1) {
                down = false;
                just_up = true;
                b = {mx, my};
            }
        }

        MonitorInfo tbm = tb_mon();
        int tbw = 280, tbh = 48;
        Rect toolbar{tbm.x - vs.x + (tbm.w - tbw) / 2, tbm.y - vs.y + 18, tbw, tbh};
        auto icon_r = [&](int i) { return Rect{toolbar.x + 10 + i * 52, toolbar.y + 4, 44, 40}; };
        bool over_tb = toolbar.contains(mx, my);

        if (just_down && over_tb) {
            down = false;
            lasso.clear();
            drawing_rect = false;
        } else if (just_down && omode == SnipMode::Rectangle) {
            drawing_rect = true;
            a = down_pt;
            sel = {};
            has_sel = false;
        }

        if (omode == SnipMode::Rectangle && drawing_rect && (down || just_up)) {
            sel = rect_from_points(a, {mx, my}, shot.w, shot.h);
            has_sel = !sel.empty();
        }

        if (just_up && over_tb) {
            for (int i = 0; i < 5; ++i) {
                if (icon_r(i).contains(mx, my)) {
                    if (i == 0) omode = SnipMode::Rectangle;
                    if (i == 1) {
                        omode = SnipMode::Freeform;
                        clear_rect_sel();
                    }
                    if (i == 2) {
                        omode = SnipMode::Window;
                        clear_rect_sel();
                    }
                    if (i == 3) {
                        omode = SnipMode::Fullscreen;
                        result = shot;
                        done = true;
                    }
                    if (i == 4) cancel = true;
                }
            }
        }

        if (omode == SnipMode::Window) {
            hover = -1;
            for (int i = int(tops.size()) - 1; i >= 0; --i) {
                Rect wr{tops[i].x - vs.x, tops[i].y - vs.y, tops[i].w, tops[i].h};
                if (wr.contains(mx, my) && !over_tb) {
                    hover = i;
                    break;
                }
            }
        }

        if (just_up && omode == SnipMode::Rectangle && drawing_rect && !over_tb) {
            sel = rect_from_points(a, {mx, my}, shot.w, shot.h);
            has_sel = sel.w >= 8 && sel.h >= 8;
            drawing_rect = false;
            if (has_sel) commit_rect();
            else {
                has_sel = false;
                sel = {};
            }
        } else if (just_up) {
            drawing_rect = false;
        }

        if (just_up && !over_tb) {
            if (omode == SnipMode::Freeform && lasso.size() >= 8) {
                int minx = shot.w, miny = shot.h, maxx = 0, maxy = 0;
                for (auto pt : lasso) {
                    minx = std::min(minx, pt.x);
                    miny = std::min(miny, pt.y);
                    maxx = std::max(maxx, pt.x);
                    maxy = std::max(maxy, pt.y);
                }
                minx = clampi(minx - 1, 0, shot.w);
                miny = clampi(miny - 1, 0, shot.h);
                maxx = clampi(maxx + 1, 0, shot.w);
                maxy = clampi(maxy + 1, 0, shot.h);
                Image cut = shot.crop(minx, miny, maxx - minx, maxy - miny);
                for (auto& pt : lasso) {
                    pt.x -= minx;
                    pt.y -= miny;
                }
                cut.apply_mask_outside(lasso);
                result = std::move(cut);
                done = true;
            } else if (omode == SnipMode::Window && hover >= 0) {
                Rect wr{tops[hover].x - vs.x, tops[hover].y - vs.y, tops[hover].w, tops[hover].h};
                wr = wr.intersect({0, 0, shot.w, shot.h});
                if (wr.w >= 3 && wr.h >= 3) {
                    result = shot.crop(wr.x, wr.y, wr.w, wr.h);
                    done = true;
                }
            }
        }

        fb.blit(dim, 0, 0);
        if (omode == SnipMode::Window && hover >= 0) {
            Rect wr{tops[hover].x - vs.x, tops[hover].y - vs.y, tops[hover].w, tops[hover].h};
            wr = wr.intersect({0, 0, fb.w, fb.h});
            fb.blit(shot, wr.x, wr.y, wr.w, wr.h, wr.x, wr.y);
            fb.stroke_rect(wr, 3, theme::selection);
        }
        if (omode == SnipMode::Rectangle && has_sel && !sel.empty()) {
            Rect r = sel.intersect({0, 0, fb.w, fb.h});
            if (!r.empty()) {
                fb.blit(shot, r.x, r.y, r.w, r.h, r.x, r.y);
                fb.stroke_rect(r, 2, theme::white);
                char lab[40];
                std::snprintf(lab, sizeof(lab), "%d × %d", r.w, r.h);
                int tw = Font::instance().width(lab, 13) + 12;
                int lh = font_h(13) + 8;
                int ly = r.y - lh - 4;
                if (ly < 4) ly = r.b() + 4;
                fb.fill_round_rect({r.x, ly, tw, lh}, 4, rgba(0, 0, 0, 210));
                Font::instance().draw(fb, r.x + 6, text_y(ly, lh, 13), lab, 13, theme::white);
            }
        }
        if (omode == SnipMode::Freeform && lasso.size() > 1 && !over_tb) fb.stroke_polyline(lasso, 2.f, theme::white, false);

        bool show_loupe = omode == SnipMode::Freeform || omode == SnipMode::Rectangle;
        if (show_loupe && !over_tb) {
            const int src = 17, mag = 4;
            int dw = src * mag;
            int lx = mx + 28, ly = my + 28;
            if (lx + dw + 10 > fb.w) lx = mx - dw - 28;
            if (ly + dw + 10 > fb.h) ly = my - dw - 28;
            fb.fill_circle(lx + dw / 2, ly + dw / 2, dw / 2 + 3, theme::white);
            for (int yy = 0; yy < dw; ++yy)
                for (int xx = 0; xx < dw; ++xx) {
                    if (std::hypot(xx - dw / 2.f, yy - dw / 2.f) > dw / 2.f - 1) continue;
                    fb.set(lx + xx, ly + yy, with_alpha(shot.get(mx - src / 2 + xx / mag, my - src / 2 + yy / mag), 255));
                }
            fb.stroke_line(lx + dw / 2, ly + 6, lx + dw / 2, ly + dw - 6, 1, with_alpha(theme::danger, 200));
            fb.stroke_line(lx + 6, ly + dw / 2, lx + dw - 6, ly + dw / 2, 1, with_alpha(theme::danger, 200));
        }

        if (omode == SnipMode::Rectangle) {
            const char* hint = "Drag to select an area";
            int hw = Font::instance().width(hint, 13) + 20;
            int hh = font_h(13) + 10;
            int hx = tbm.x - vs.x + (tbm.w - hw) / 2;
            int hy = tbm.y - vs.y + tbm.h - hh - 16;
            if (hy < toolbar.b() + 8) hy = toolbar.b() + 8;
            fb.fill_round_rect({hx, hy, hw, hh}, 6, rgba(0, 0, 0, 200));
            Font::instance().draw(fb, hx + 10, text_y(hy, hh, 13), hint, 13, theme::white);
        }

        fb.fill_round_rect(toolbar, 12, rgba(28, 28, 28, 242));
        fb.stroke_round_rect(toolbar, 12, 1, rgba(255, 255, 255, 28));
        Icon ics[5] = {Icon::Rect, Icon::Freeform, Icon::Window, Icon::Full, Icon::Close};
        SnipMode ms[4] = {SnipMode::Rectangle, SnipMode::Freeform, SnipMode::Window, SnipMode::Fullscreen};
        for (int i = 0; i < 5; ++i) {
            Rect ir = icon_r(i);
            bool on = i < 4 && omode == ms[i];
            if (on) fb.fill_round_rect(ir, 8, theme::bg_selected);
            else if (ir.contains(mx, my)) fb.fill_round_rect(ir, 8, theme::bg_button_hot);
            draw_icon(fb, ir.inset(8), ics[i], i == 4 ? theme::text : (on ? theme::accent : theme::text));
        }

        if (overlay_dirty) {
            p->present(WinKind::Overlay, fb);
            overlay_dirty = false;
        }
        p->wait_ms(just_down || just_up || down ? 8 : 16);
    }

    p->destroy_window(WinKind::Overlay);
    p->set_cursor(CursorType::Arrow);
    mode = omode;
    return result;
}

}  // namespace

int run_app(Platform* plat, const CliOptions& opt) {
    App app;
    app.p = plat;
    app.opt = opt;
    app.mode = opt.mode;
    app.delay_sec = opt.delay;
    return app.run();
}

void print_help(const char* argv0) {
    std::printf(
        "AlphaSnipe — screenshot utility\n\n"
        "Usage: %s [options]\n\n"
        "  --new                 Start a snip immediately\n"
        "  --mode MODE           rect | freeform | window | full\n"
        "  --delay N             Delay N seconds before capture (0, 3, 5, 10)\n"
        "  --demo                Use a demo desktop instead of a live capture\n"
        "  --output FILE         Capture fullscreen and save PNG, then exit\n"
        "  --region x,y,w,h      Capture a rectangle and save (requires --output)\n"
        "  --self-test           Run unit tests and exit\n"
        "  --help                Show this help\n\n"
        "Shortcuts:\n"
        "  Ctrl+Shift+S          New snip\n"
        "  Ctrl+C / Ctrl+S       Copy / Save\n"
        "  Ctrl+Z                Undo ink\n"
        "  P / H / E / D         Pen / Highlighter / Eraser / Drag-and-drop\n"
        "  Esc                   Cancel snip, crop, or drag selection\n",
        argv0);
}

bool parse_cli(int argc, char** argv, CliOptions& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            print_help(argv[0]);
            std::exit(0);
        } else if (a == "--self-test")
            opt.self_test = true;
        else if (a == "--demo")
            opt.demo = true;
        else if (a == "--new")
            opt.start_new = true;
        else if (a == "--mode") {
            const char* v = need("--mode");
            if (!v) return false;
            std::string m = v;
            if (m == "rect" || m == "rectangle") opt.mode = SnipMode::Rectangle;
            else if (m == "freeform")
                opt.mode = SnipMode::Freeform;
            else if (m == "window")
                opt.mode = SnipMode::Window;
            else if (m == "full" || m == "fullscreen")
                opt.mode = SnipMode::Fullscreen;
            else {
                std::fprintf(stderr, "unknown mode %s\n", v);
                return false;
            }
        } else if (a == "--delay") {
            const char* v = need("--delay");
            if (!v) return false;
            opt.delay = std::atoi(v);
        } else if (a == "--output" || a == "-o") {
            const char* v = need("--output");
            if (!v) return false;
            opt.output = v;
            opt.headless = true;
        } else if (a == "--region") {
            const char* v = need("--region");
            if (!v) return false;
            int x, y, w, h;
            if (std::sscanf(v, "%d,%d,%d,%d", &x, &y, &w, &h) != 4) {
                std::fprintf(stderr, "region must be x,y,w,h\n");
                return false;
            }
            opt.region = {x, y, w, h};
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return false;
        }
    }
    return true;
}

int run_self_test() {
    int fails = 0;
    auto check = [&](bool c, const char* msg) {
        if (!c) {
            std::fprintf(stderr, "FAIL: %s\n", msg);
            ++fails;
        } else
            std::printf("ok  %s\n", msg);
    };

    Image a(32, 32);
    a.fill(rgba(10, 20, 30));
    check(a.get(3, 3) == rgba(10, 20, 30), "fill/get");
    a.set(4, 5, rgba(255, 0, 0));
    check(cr(a.get(4, 5)) == 255, "set pixel");
    Image b = a.crop(2, 2, 10, 10);
    check(b.w == 10 && b.h == 10, "crop size");
    a.fill_rect({0, 0, 8, 8}, rgba(0, 255, 0));
    check(cg(a.get(1, 1)) == 255, "fill_rect");
    a.fill_circle(16, 16, 5, rgba(0, 0, 255));
    check(cb(a.get(16, 16)) == 255, "fill_circle");
    a.stroke_line(0, 0, 31, 31, 2, rgba(255, 255, 0));
    check(cr(a.get(8, 8)) > 100, "stroke_line");

    Image demo = Image::make_demo(320, 200);
    check(demo.valid() && !is_blank(demo), "demo image");
    std::vector<uint8_t> png;
    check(demo.encode_png(png) && png.size() > 32 && png[0] == 0x89 && png[1] == 'P', "png encode");

    std::vector<Point> poly = {{4, 4}, {20, 4}, {20, 20}, {4, 20}};
    Image mask(24, 24);
    mask.fill(rgba(255, 255, 255));
    mask.apply_mask_outside(poly);
    check(ca(mask.get(10, 10)) == 255 && ca(mask.get(1, 1)) == 0, "freeform mask");

    Image ink(32, 32);
    ink.fill(rgba(0, 0, 0, 0));
    raster_stroke(ink, {{2, 2}, {20, 8}, {8, 20}}, 4, rgba(255, 0, 0), false);
    int painted = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (ca(ink.get(x, y)) > 0) ++painted;
    check(painted > 20, "pen raster");
    raster_stroke(ink, {{2, 2}, {20, 8}}, 8, 0, true);
    check(true, "eraser raster");

    Font::load_app_fonts(".");
    check(Font::instance().ok() || Font::load_app_fonts("/usr/share/fonts/truetype/dejavu"), "font load");
    if (Font::instance().ok()) {
        int tw = Font::instance().width("AlphaSnipe", 18);
        check(tw > 40, "font width");
        Image t(200, 40);
        t.fill(theme::bg);
        Font::instance().draw(t, 4, 4, "Hello", 18, theme::text);
        check(true, "font draw");
    }

    Image rr(80, 40);
    rr.fill(theme::bg);
    rr.fill_round_rect({4, 4, 72, 32}, 8, theme::accent);
    check(cb(rr.get(40, 20)) > 100, "round rect");

    Image ic(32, 32);
    ic.fill(theme::bg_toolbar);
    draw_icon(ic, {0, 0, 32, 32}, Icon::DragDrop, theme::text);
    int opaque = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            uint32_t c = ic.get(x, y);
            if (ca(c) == 255 && cr(c) > 200 && cg(c) > 200 && cb(c) > 200) ++opaque;
        }
    check(opaque > 20, "drag-drop icon");

    Rect rp = rect_from_points({100, 80}, {40, 20}, 200, 150);
    check(rp.x == 40 && rp.y == 20 && rp.w == 60 && rp.h == 60, "rect from points");
    Rect rh = apply_sel_handle({10, 10, 40, 40}, 4, 15, 10, 200, 150);
    check(rh.w == 55 && rh.h == 50, "rect handle resize");
    check(sel_handle_at({10, 10, 40, 40}, 10, 10) == 0, "rect handle hit nw");
    check(sel_handle_at({10, 10, 40, 40}, 30, 30) == 8, "rect interior hit");

    if (fails == 0) std::printf("All self-tests passed.\n");
    else std::printf("%d test(s) failed.\n", fails);
    return fails ? 1 : 0;
}
