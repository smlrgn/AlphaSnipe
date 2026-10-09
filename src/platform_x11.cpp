#include "platform.hpp"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <limits.h>
#include <pwd.h>
#include <fcntl.h>

namespace {

int xerr(Display*, XErrorEvent*) { return 0; }

int shift_of(unsigned long mask) {
    if (!mask) return 0;
    int s = 0;
    while (((mask >> s) & 1) == 0) ++s;
    return s;
}

KeyId map_key(KeySym ks) {
    switch (ks) {
        case XK_Escape: return KeyId::Esc;
        case XK_Return:
        case XK_KP_Enter: return KeyId::Return;
        case XK_space: return KeyId::Space;
        case XK_BackSpace: return KeyId::Backspace;
        case XK_Delete: return KeyId::Delete;
        case XK_Tab: return KeyId::Tab;
        case XK_Left: return KeyId::Left;
        case XK_Right: return KeyId::Right;
        case XK_Up: return KeyId::Up;
        case XK_Down: return KeyId::Down;
        case XK_Home: return KeyId::Home;
        case XK_End: return KeyId::End;
        case XK_a:
        case XK_A: return KeyId::A;
        case XK_c:
        case XK_C: return KeyId::C;
        case XK_d:
        case XK_D: return KeyId::D;
        case XK_e:
        case XK_E: return KeyId::E;
        case XK_h:
        case XK_H: return KeyId::H;
        case XK_n:
        case XK_N: return KeyId::N;
        case XK_o:
        case XK_O: return KeyId::O;
        case XK_p:
        case XK_P: return KeyId::P;
        case XK_s:
        case XK_S: return KeyId::S;
        case XK_v:
        case XK_V: return KeyId::V;
        case XK_x:
        case XK_X: return KeyId::X;
        case XK_y:
        case XK_Y: return KeyId::Y;
        case XK_z:
        case XK_Z: return KeyId::Z;
        case XK_1:
        case XK_KP_1: return KeyId::Num1;
        case XK_2:
        case XK_KP_2: return KeyId::Num2;
        case XK_3:
        case XK_KP_3: return KeyId::Num3;
        case XK_4:
        case XK_KP_4: return KeyId::Num4;
        case XK_F1: return KeyId::F1;
        default: return KeyId::Unknown;
    }
}

struct XW {
    Window win = 0;
    int w = 0, h = 0;
    bool mapped = false;
    Image last;
};

class X11Platform final : public Platform {
public:
    Display* dpy = nullptr;
    int screen = 0;
    Window root = 0;
    Visual* vis = nullptr;
    int depth = 24;
    unsigned long rmask = 0, gmask = 0, bmask = 0;
    int rshift = 0, gshift = 0, bshift = 0;
    Colormap cmap = 0;
    Atom wm_delete = 0, wm_protocols = 0, utf8_string = 0;
    Atom clipboard = 0, targets = 0, png_atom = 0, incr = 0;
    Atom uri_list = 0, text_plain = 0, text_plain_utf8 = 0, moz_url = 0;
    Atom xdnd_aware = 0, xdnd_enter = 0, xdnd_position = 0, xdnd_status = 0, xdnd_leave = 0;
    Atom xdnd_drop = 0, xdnd_finished = 0, xdnd_selection = 0, xdnd_typelist = 0, xdnd_action_copy = 0;
    std::vector<uint8_t> drag_png;
    std::string drag_uri;
    std::string drag_uri_plain;
    std::string drag_path;
    bool has_drag = false;
    Cursor cursors[10]{};
    XW wins[4];
    std::vector<uint8_t> clip_png;
    Image clip_img;
    bool has_clip = false;
    std::string exe;
    CursorType current_cursor = CursorType::Arrow;

    ~X11Platform() override { shutdown(); }

    int idx(WinKind k) const { return int(k); }

    bool init() override {
        dpy = XOpenDisplay(nullptr);
        if (!dpy) return false;
        XSetErrorHandler(xerr);
        screen = DefaultScreen(dpy);
        root = RootWindow(dpy, screen);
        vis = DefaultVisual(dpy, screen);
        depth = DefaultDepth(dpy, screen);
        cmap = DefaultColormap(dpy, screen);
        rmask = vis->red_mask;
        gmask = vis->green_mask;
        bmask = vis->blue_mask;
        rshift = shift_of(rmask);
        gshift = shift_of(gmask);
        bshift = shift_of(bmask);
        wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
        utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
        clipboard = XInternAtom(dpy, "CLIPBOARD", False);
        targets = XInternAtom(dpy, "TARGETS", False);
        png_atom = XInternAtom(dpy, "image/png", False);
        incr = XInternAtom(dpy, "INCR", False);
        uri_list = XInternAtom(dpy, "text/uri-list", False);
        text_plain = XInternAtom(dpy, "text/plain", False);
        text_plain_utf8 = XInternAtom(dpy, "text/plain;charset=utf-8", False);
        moz_url = XInternAtom(dpy, "text/x-moz-url", False);
        xdnd_aware = XInternAtom(dpy, "XdndAware", False);
        xdnd_enter = XInternAtom(dpy, "XdndEnter", False);
        xdnd_position = XInternAtom(dpy, "XdndPosition", False);
        xdnd_status = XInternAtom(dpy, "XdndStatus", False);
        xdnd_leave = XInternAtom(dpy, "XdndLeave", False);
        xdnd_drop = XInternAtom(dpy, "XdndDrop", False);
        xdnd_finished = XInternAtom(dpy, "XdndFinished", False);
        xdnd_selection = XInternAtom(dpy, "XdndSelection", False);
        xdnd_typelist = XInternAtom(dpy, "XdndTypeList", False);
        xdnd_action_copy = XInternAtom(dpy, "XdndActionCopy", False);
        cursors[int(CursorType::Arrow)] = XCreateFontCursor(dpy, XC_left_ptr);
        cursors[int(CursorType::Cross)] = XCreateFontCursor(dpy, XC_crosshair);
        cursors[int(CursorType::Hand)] = XCreateFontCursor(dpy, XC_hand2);
        cursors[int(CursorType::IBeam)] = XCreateFontCursor(dpy, XC_xterm);
        cursors[int(CursorType::SizeAll)] = XCreateFontCursor(dpy, XC_fleur);
        cursors[int(CursorType::SizeNwse)] = XCreateFontCursor(dpy, XC_bottom_left_corner);
        cursors[int(CursorType::SizeNesw)] = XCreateFontCursor(dpy, XC_bottom_right_corner);
        cursors[int(CursorType::SizeWe)] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
        cursors[int(CursorType::SizeNs)] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
        cursors[int(CursorType::Hidden)] = None;

        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            exe = buf;
            auto slash = exe.find_last_of('/');
            if (slash != std::string::npos) exe.resize(slash);
        } else {
            exe = ".";
        }
        return true;
    }

    void forget_drop_file() {
        if (!drag_path.empty()) {
            unlink(drag_path.c_str());
            drag_path.clear();
        }
        drag_uri.clear();
        drag_uri_plain.clear();
        drag_png.clear();
        has_drag = false;
    }

    static std::string file_uri_from_path(const std::string& path) {
        std::string u = "file://";
        for (unsigned char c : path) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                c == '-' || c == '_' || c == '~')
                u += char(c);
            else {
                char b[8];
                std::snprintf(b, sizeof(b), "%%%02X", c);
                u += b;
            }
        }
        return u;
    }

    void shutdown() override {
        forget_drop_file();
        if (!dpy) return;
        for (int i = 0; i < 4; ++i) destroy_window(WinKind(i));
        XCloseDisplay(dpy);
        dpy = nullptr;
    }

    Rect virtual_screen() override {
        return {0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen)};
    }

    std::vector<MonitorInfo> monitors() override {
        Rect vs = virtual_screen();
        return {{vs.x, vs.y, vs.w, vs.h, true}};
    }

    void mouse_screen_pos(int& x, int& y) override {
        Window rr, cr;
        int rx, ry, wx, wy;
        unsigned m;
        XQueryPointer(dpy, root, &rr, &cr, &rx, &ry, &wx, &wy, &m);
        x = rx;
        y = ry;
    }

    Image ximage_to_rgba(XImage* img) {
        Image out(img->width, img->height);
        for (int y = 0; y < img->height; ++y) {
            for (int x = 0; x < img->width; ++x) {
                unsigned long p = 0;
                if (img->bits_per_pixel == 32 && img->red_mask && img->green_mask && img->blue_mask) {
                    auto* row = reinterpret_cast<uint32_t*>(img->data + y * img->bytes_per_line);
                    p = row[x];
                    uint8_t r = uint8_t((p & img->red_mask) >> shift_of(img->red_mask));
                    uint8_t g = uint8_t((p & img->green_mask) >> shift_of(img->green_mask));
                    uint8_t b = uint8_t((p & img->blue_mask) >> shift_of(img->blue_mask));
                    out.set(x, y, rgba(r, g, b));
                } else if (img->bits_per_pixel == 32 && !img->red_mask) {
                    // packed BGRX little-endian common on Xorg
                    uint8_t* row = reinterpret_cast<uint8_t*>(img->data + y * img->bytes_per_line);
                    uint8_t b = row[x * 4 + 0];
                    uint8_t g = row[x * 4 + 1];
                    uint8_t r = row[x * 4 + 2];
                    out.set(x, y, rgba(r, g, b));
                } else {
                    p = XGetPixel(img, x, y);
                    uint8_t r = rmask ? uint8_t((p & rmask) >> rshift) : 0;
                    uint8_t g = gmask ? uint8_t((p & gmask) >> gshift) : 0;
                    uint8_t b = bmask ? uint8_t((p & bmask) >> bshift) : 0;
                    out.set(x, y, rgba(r, g, b));
                }
            }
        }
        return out;
    }

    Image capture_window_image(Window win, int ww, int hh) {
        if (ww <= 0 || hh <= 0) return {};
        XImage* img = XGetImage(dpy, win, 0, 0, ww, hh, AllPlanes, ZPixmap);
        if (!img) return {};
        Image out = ximage_to_rgba(img);
        XDestroyImage(img);
        return out;
    }

    Image capture_screen() override {
        Rect vs = virtual_screen();
        // Hide our windows so they are not in the shot.
        bool vis_state[4]{};
        for (int i = 0; i < 4; ++i) {
            vis_state[i] = wins[i].mapped;
            if (wins[i].mapped) {
                XUnmapWindow(dpy, wins[i].win);
                wins[i].mapped = false;
            }
        }
        XSync(dpy, False);
        usleep(80000);

        Image out;
        Pixmap pm = XCreatePixmap(dpy, root, vs.w, vs.h, depth);
        GC gc = XCreateGC(dpy, pm, 0, nullptr);
        XCopyArea(dpy, root, pm, gc, 0, 0, vs.w, vs.h, 0, 0);
        XSync(dpy, False);
        out = capture_window_image(pm, vs.w, vs.h);
        XFreeGC(dpy, gc);
        XFreePixmap(dpy, pm);

        bool blank = true;
        if (out.valid()) {
            for (int i = 0; i < std::min(2000, vs.w * vs.h); ++i) {
                int x = (i * 97) % vs.w, y = (i * 53) % vs.h;
                if ((out.get(x, y) & 0x00FFFFFFu) != 0) {
                    blank = false;
                    break;
                }
            }
        }
        if (!out.valid() || blank) {
            // Composite mapped top-level windows (works when root is redirected).
            out.resize(vs.w, vs.h);
            out.fill(rgba(0, 0, 0));
            Window rr, rp, *children = nullptr;
            unsigned n = 0;
            if (XQueryTree(dpy, root, &rr, &rp, &children, &n) && children) {
                for (unsigned i = 0; i < n; ++i) {
                    XWindowAttributes wa{};
                    if (!XGetWindowAttributes(dpy, children[i], &wa)) continue;
                    if (wa.map_state != IsViewable || wa.width < 2 || wa.height < 2) continue;
                    bool ours = false;
                    for (int k = 0; k < 4; ++k)
                        if (wins[k].win == children[i]) ours = true;
                    if (ours) continue;
                    Image wimg = capture_window_image(children[i], wa.width, wa.height);
                    if (wimg.valid()) out.blit(wimg, wa.x, wa.y);
                }
                XFree(children);
            }
        }

        for (int i = 0; i < 4; ++i) {
            if (vis_state[i] && wins[i].win) {
                XMapRaised(dpy, wins[i].win);
                wins[i].mapped = true;
            }
        }
        XFlush(dpy);
        return out;
    }

    std::vector<TopWindow> windows() override {
        std::vector<TopWindow> list;
        Atom net = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", True);
        if (!net) net = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
        if (net) {
            Atom type;
            int fmt;
            unsigned long nitems = 0, bytes = 0;
            unsigned char* prop = nullptr;
            if (XGetWindowProperty(dpy, root, net, 0, 4096, False, XA_WINDOW, &type, &fmt, &nitems, &bytes, &prop) ==
                    Success &&
                prop) {
                auto* ws = reinterpret_cast<Window*>(prop);
                for (unsigned long i = 0; i < nitems; ++i) {
                    XWindowAttributes wa{};
                    if (!XGetWindowAttributes(dpy, ws[i], &wa) || wa.map_state != IsViewable) continue;
                    int rx = 0, ry = 0;
                    Window child;
                    XTranslateCoordinates(dpy, ws[i], root, 0, 0, &rx, &ry, &child);
                    char* name = nullptr;
                    XFetchName(dpy, ws[i], &name);
                    TopWindow t;
                    t.x = rx;
                    t.y = ry;
                    t.w = wa.width;
                    t.h = wa.height;
                    t.title = name ? name : "";
                    t.id = uintptr_t(ws[i]);
                    if (name) XFree(name);
                    bool ours = false;
                    for (int k = 0; k < 4; ++k)
                        if (wins[k].win == ws[i]) ours = true;
                    if (!ours && t.w > 8 && t.h > 8) list.push_back(std::move(t));
                }
                XFree(prop);
            }
        }
        return list;
    }

    bool create_window(const WindowDesc& d) override {
        destroy_window(d.kind);
        XSetWindowAttributes swa{};
        unsigned long mask = CWEventMask | CWBackPixel | CWColormap | CWBorderPixel;
        swa.colormap = cmap;
        swa.background_pixel = BlackPixel(dpy, screen);
        swa.border_pixel = 0;
        swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | StructureNotifyMask | FocusChangeMask | PropertyChangeMask;
        if (d.borderless) {
            swa.override_redirect = True;
            mask |= CWOverrideRedirect;
        }
        if (d.always_on_top) {
            swa.save_under = True;
            mask |= CWSaveUnder;
        }
        Window w = XCreateWindow(dpy, root, d.x, d.y, d.w, d.h, 0, depth, InputOutput, vis, mask, &swa);
        XStoreName(dpy, w, d.title);
        XSetWMProtocols(dpy, w, &wm_delete, 1);

        if (!d.borderless) {
            XSizeHints hints{};
            hints.flags = PMinSize | PSize;
            hints.min_width = 480;
            hints.min_height = 360;
            hints.width = d.w;
            hints.height = d.h;
            if (!d.resizable) {
                hints.flags |= PMaxSize;
                hints.max_width = d.w;
                hints.max_height = d.h;
                hints.min_width = d.w;
                hints.min_height = d.h;
            }
            XSetWMNormalHints(dpy, w, &hints);
        } else {
            Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
            Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
            Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
            Atom vals[2] = {above, fullscreen};
            XChangeProperty(dpy, w, state, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(vals), 2);
            Atom type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
            Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
            XChangeProperty(dpy, w, type, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&dock), 1);
        }

        XClassHint ch{};
        ch.res_name = const_cast<char*>("alphasnipe");
        ch.res_class = const_cast<char*>("AlphaSnipe");
        XSetClassHint(dpy, w, &ch);

        XW rec;
        rec.win = w;
        rec.w = d.w;
        rec.h = d.h;
        wins[idx(d.kind)] = rec;
        if (d.visible) {
            XMapRaised(dpy, w);
            rec.mapped = true;
            wins[idx(d.kind)] = rec;
            if (d.kind == WinKind::Overlay) {
                XGrabPointer(dpy, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                             GrabModeAsync, w, cursors[int(CursorType::Cross)], CurrentTime);
                XGrabKeyboard(dpy, w, True, GrabModeAsync, GrabModeAsync, CurrentTime);
            }
        }
        XFlush(dpy);
        return true;
    }

    void destroy_window(WinKind k) override {
        XW& rec = wins[idx(k)];
        if (!rec.win) return;
        if (k == WinKind::Overlay) {
            XUngrabPointer(dpy, CurrentTime);
            XUngrabKeyboard(dpy, CurrentTime);
        }
        XDestroyWindow(dpy, rec.win);
        rec = {};
        XFlush(dpy);
    }

    void show_window(WinKind k, bool show) override {
        XW& rec = wins[idx(k)];
        if (!rec.win) return;
        if (show) {
            XMapRaised(dpy, rec.win);
            rec.mapped = true;
            if (k == WinKind::Overlay) {
                XGrabPointer(dpy, rec.win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                             GrabModeAsync, rec.win, None, CurrentTime);
                XGrabKeyboard(dpy, rec.win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
            }
        } else {
            if (k == WinKind::Overlay) {
                XUngrabPointer(dpy, CurrentTime);
                XUngrabKeyboard(dpy, CurrentTime);
            }
            XUnmapWindow(dpy, rec.win);
            rec.mapped = false;
        }
        XFlush(dpy);
    }

    void set_title(WinKind k, const char* title) override {
        if (wins[idx(k)].win) XStoreName(dpy, wins[idx(k)].win, title);
    }

    void set_size(WinKind k, int w, int h) override {
        XW& rec = wins[idx(k)];
        if (!rec.win) return;
        XResizeWindow(dpy, rec.win, w, h);
        rec.w = w;
        rec.h = h;
    }

    void get_size(WinKind k, int& w, int& h) override {
        w = wins[idx(k)].w;
        h = wins[idx(k)].h;
    }

    void present(WinKind k, const Image& img) override {
        XW& rec = wins[idx(k)];
        if (!rec.win || !img.valid()) return;
        rec.last = img;
        rec.w = img.w;
        rec.h = img.h;
        std::vector<uint8_t> native(size_t(img.w) * size_t(img.h) * 4);
        for (int y = 0; y < img.h; ++y) {
            const uint8_t* s = img.row(y);
            uint8_t* d = native.data() + size_t(y) * size_t(img.w) * 4;
            for (int x = 0; x < img.w; ++x) {
                uint8_t r = s[x * 4 + 0], g = s[x * 4 + 1], b = s[x * 4 + 2];
                uint32_t pix = (uint32_t(r) << rshift) | (uint32_t(g) << gshift) | (uint32_t(b) << bshift);
                // Default TrueColor is often BGRX in memory via this packing; write as 32-bit LE.
                reinterpret_cast<uint32_t*>(d)[x] = pix;
            }
        }
        XImage* ximg = XCreateImage(dpy, vis, depth, ZPixmap, 0, reinterpret_cast<char*>(native.data()), img.w, img.h,
                                    32, img.w * 4);
        if (!ximg) return;
        ximg->byte_order = LSBFirst;
        ximg->bitmap_bit_order = LSBFirst;
        ximg->bits_per_pixel = 32;
        ximg->red_mask = rmask;
        ximg->green_mask = gmask;
        ximg->blue_mask = bmask;
        GC gc = DefaultGC(dpy, screen);
        XPutImage(dpy, rec.win, gc, ximg, 0, 0, 0, 0, unsigned(img.w), unsigned(img.h));
        ximg->data = nullptr;  // we own native
        XDestroyImage(ximg);
        XFlush(dpy);
    }

    WinKind kind_of(Window w) {
        for (int i = 0; i < 4; ++i)
            if (wins[i].win == w) return WinKind(i);
        return WinKind::Main;
    }

    void serve_clipboard(XSelectionRequestEvent* req) {
        XSelectionEvent ev{};
        ev.type = SelectionNotify;
        ev.display = req->display;
        ev.requestor = req->requestor;
        ev.selection = req->selection;
        ev.target = req->target;
        ev.time = req->time;
        ev.property = None;
        Atom prop = req->property ? req->property : req->target;

        auto put8 = [&](Atom type, const void* data, int n) {
            XChangeProperty(dpy, req->requestor, prop, type, 8, PropModeReplace,
                            const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data)), n);
            ev.property = prop;
        };

        bool drag_sel = has_drag && req->selection == xdnd_selection;
        if (drag_sel) {
            if (req->target == targets) {
                Atom list[8] = {targets, uri_list, text_plain, text_plain_utf8, utf8_string, moz_url, XA_STRING,
                                png_atom};
                XChangeProperty(dpy, req->requestor, prop, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<unsigned char*>(list), 8);
                ev.property = prop;
            } else if (req->target == uri_list && !drag_uri.empty()) {
                put8(uri_list, drag_uri.data(), int(drag_uri.size()));
            } else if (req->target == moz_url && !drag_uri_plain.empty()) {
                std::string moz = drag_uri_plain + "\nScreenshot.png";
                put8(moz_url, moz.data(), int(moz.size()));
            } else if ((req->target == text_plain || req->target == text_plain_utf8 || req->target == utf8_string ||
                        req->target == XA_STRING) &&
                       !drag_uri_plain.empty()) {
                put8(req->target, drag_uri_plain.data(), int(drag_uri_plain.size()));
            } else if (req->target == png_atom && !drag_png.empty()) {
                put8(png_atom, drag_png.data(), int(drag_png.size()));
            }
            XSendEvent(dpy, req->requestor, True, NoEventMask, reinterpret_cast<XEvent*>(&ev));
            return;
        }

        if (!has_clip) {
            XSendEvent(dpy, req->requestor, True, NoEventMask, reinterpret_cast<XEvent*>(&ev));
            return;
        }
        if (req->target == targets) {
            Atom list[4] = {targets, png_atom, XA_STRING, utf8_string};
            XChangeProperty(dpy, req->requestor, prop, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<unsigned char*>(list), 4);
            ev.property = prop;
        } else if (req->target == png_atom && !clip_png.empty()) {
            put8(png_atom, clip_png.data(), int(clip_png.size()));
        }
        XSendEvent(dpy, req->requestor, True, NoEventMask, reinterpret_cast<XEvent*>(&ev));
    }

    bool poll(PlatEvent& e) override {
        e = {};
        if (!XPending(dpy)) return false;
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case ClientMessage:
                if (Atom(ev.xclient.data.l[0]) == wm_delete) {
                    e.type = PlatEvent::Close;
                    e.win = kind_of(ev.xclient.window);
                    return true;
                }
                break;
            case ConfigureNotify: {
                XW& rec = wins[idx(kind_of(ev.xconfigure.window))];
                if (rec.win && (rec.w != ev.xconfigure.width || rec.h != ev.xconfigure.height)) {
                    rec.w = ev.xconfigure.width;
                    rec.h = ev.xconfigure.height;
                    e.type = PlatEvent::Resize;
                    e.win = kind_of(ev.xconfigure.window);
                    e.width = rec.w;
                    e.height = rec.h;
                    return true;
                }
                break;
            }
            case Expose:
                if (ev.xexpose.count == 0) {
                    WinKind k = kind_of(ev.xexpose.window);
                    if (wins[idx(k)].last.valid()) present(k, wins[idx(k)].last);
                }
                break;
            case MotionNotify:
                e.type = PlatEvent::MouseMove;
                e.win = kind_of(ev.xmotion.window);
                e.x = ev.xmotion.x;
                e.y = ev.xmotion.y;
                return true;
            case ButtonPress:
            case ButtonRelease:
                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) {
                    if (ev.type == ButtonPress) {
                        e.type = PlatEvent::Wheel;
                        e.win = kind_of(ev.xbutton.window);
                        e.x = ev.xbutton.x;
                        e.y = ev.xbutton.y;
                        e.dy = ev.xbutton.button == 4 ? 1 : -1;
                        return true;
                    }
                    break;
                }
                e.type = ev.type == ButtonPress ? PlatEvent::MouseDown : PlatEvent::MouseUp;
                e.win = kind_of(ev.xbutton.window);
                e.x = ev.xbutton.x;
                e.y = ev.xbutton.y;
                e.button = int(ev.xbutton.button);
                return true;
            case KeyPress: {
                char buf[8]{};
                KeySym ks = 0;
                int n = XLookupString(&ev.xkey, buf, 7, &ks, nullptr);
                e.win = kind_of(ev.xkey.window);
                e.mods = 0;
                if (ev.xkey.state & ShiftMask) e.mods |= Mods::Shift;
                if (ev.xkey.state & ControlMask) e.mods |= Mods::Ctrl;
                if (ev.xkey.state & Mod1Mask) e.mods |= Mods::Alt;
                if (ev.xkey.state & Mod4Mask) e.mods |= Mods::Meta;
                e.key = map_key(ks);
                e.type = PlatEvent::KeyDown;
                if (n > 0 && buf[0] >= 32 && !(e.mods & Mods::Ctrl)) {
                    // deliver key first; char via extra event stored? send Char after by synthesizing:
                    // we'll set ch as well
                    e.ch = (unsigned char)buf[0];
                }
                return true;
            }
            case KeyRelease: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                e.type = PlatEvent::KeyUp;
                e.win = kind_of(ev.xkey.window);
                e.key = map_key(ks);
                return true;
            }
            case SelectionRequest:
                serve_clipboard(&ev.xselectionrequest);
                break;
            case FocusIn:
                e.type = PlatEvent::Focus;
                e.win = kind_of(ev.xfocus.window);
                e.button = 1;
                return true;
            default:
                break;
        }
        return false;
    }

    void wait_ms(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    uint64_t ticks_ms() override {
        using namespace std::chrono;
        return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    bool copy_image(const Image& img) override {
        if (!img.encode_png(clip_png)) return false;
        clip_img = img;
        has_clip = true;
        XSetSelectionOwner(dpy, clipboard, wins[idx(WinKind::Main)].win
                                                ? wins[idx(WinKind::Main)].win
                                                : wins[idx(WinKind::Overlay)].win,
                           CurrentTime);
        // Best-effort persist via xclip / wl-copy.
        if (!clip_png.empty()) {
            FILE* f = popen("xclip -selection clipboard -t image/png >/dev/null 2>&1", "w");
            if (f) {
                fwrite(clip_png.data(), 1, clip_png.size(), f);
                pclose(f);
            } else {
                FILE* w = popen("wl-copy --type image/png >/dev/null 2>&1", "w");
                if (w) {
                    fwrite(clip_png.data(), 1, clip_png.size(), w);
                    pclose(w);
                }
            }
        }
        return true;
    }

    std::string save_dialog(const std::string& suggested, const std::string& start_dir) override {
        auto run = [&](const std::string& cmd) -> std::string {
            FILE* f = popen(cmd.c_str(), "r");
            if (!f) return {};
            char buf[4096];
            std::string out;
            while (fgets(buf, sizeof(buf), f)) out += buf;
            int st = pclose(f);
            if (st != 0) return {};
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
            return out;
        };
        std::string start = start_dir;
        if (!start.empty() && start.back() != '/') start += '/';
        start += suggested;
        std::string z = run("zenity --file-selection --save --confirm-overwrite --filename='" + start + "' 2>/dev/null");
        if (!z.empty()) return z;
        z = run("kdialog --getsavefilename '" + start + "' 'PNG image (*.png)' 2>/dev/null");
        return z;
    }

    std::string pictures_dir() override {
        const char* home = getenv("HOME");
        if (!home) {
            auto* pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : ".";
        }
        std::string pic = std::string(home) + "/Pictures";
        struct stat st {};
        if (stat(pic.c_str(), &st) != 0) pic = home;
        return pic + "/Screenshots";
    }

    bool write_file(const std::string& path, const uint8_t* data, size_t n) override {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data), std::streamsize(n));
        return bool(f);
    }

    bool make_dirs(const std::string& path) override {
        std::string cur;
        for (size_t i = 0; i <= path.size(); ++i) {
            if (i == path.size() || path[i] == '/') {
                if (cur.empty()) {
                    cur = "/";
                    continue;
                }
                mkdir(cur.c_str(), 0755);
                if (i != path.size()) cur.push_back('/');
            } else {
                if (cur == "/") cur.clear();
                cur.push_back(path[i]);
            }
        }
        return true;
    }

    void set_cursor(CursorType c) override {
        current_cursor = c;
        Cursor xc = cursors[int(c)];
        for (int i = 0; i < 4; ++i) {
            if (wins[i].win) XDefineCursor(dpy, wins[i].win, xc);
        }
        XFlush(dpy);
    }

    void beep() override { XBell(dpy, 0); }

    bool register_hotkey() override {
        auto grab = [&](int keycode, unsigned mods) {
            XGrabKey(dpy, keycode, mods, root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, keycode, mods | Mod2Mask, root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, keycode, mods | LockMask, root, True, GrabModeAsync, GrabModeAsync);
        };
        grab(XKeysymToKeycode(dpy, XK_S), ControlMask | ShiftMask);
        grab(XKeysymToKeycode(dpy, XK_Print), 0);
        XFlush(dpy);
        return true;
    }

    bool xdnd_version(Window w, int* ver) {
        Atom type = None;
        int fmt = 0;
        unsigned long n = 0, b = 0;
        unsigned char* prop = nullptr;
        if (XGetWindowProperty(dpy, w, xdnd_aware, 0, 1, False, AnyPropertyType, &type, &fmt, &n, &b, &prop) != Success ||
            !prop || n < 1) {
            if (prop) XFree(prop);
            return false;
        }
        if (ver) {
            if (fmt == 32 && n >= 1) *ver = int(*reinterpret_cast<unsigned long*>(prop));
            else *ver = int(prop[0]);
        }
        XFree(prop);
        return true;
    }

    Window find_xdnd_target(int rx, int ry, Window skip) {
        Window window = root;
        Window child = None;
        int nx = 0, ny = 0;
        for (;;) {
            if (!XTranslateCoordinates(dpy, root, window, rx, ry, &nx, &ny, &child) || !child) break;
            window = child;
        }
        while (window && window != root) {
            int ver = 0;
            if (window != skip && xdnd_version(window, &ver)) return window;
            Window rr, parent = None, *ch = nullptr;
            unsigned n = 0;
            if (!XQueryTree(dpy, window, &rr, &parent, &ch, &n)) break;
            if (ch) XFree(ch);
            if (!parent || parent == window) break;
            window = parent;
        }
        return None;
    }

    void send_xdnd(Window target, Atom type, long l0, long l1, long l2, long l3, long l4) {
        XEvent ev{};
        ev.xclient.type = ClientMessage;
        ev.xclient.display = dpy;
        ev.xclient.window = target;
        ev.xclient.message_type = type;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = l0;
        ev.xclient.data.l[1] = l1;
        ev.xclient.data.l[2] = l2;
        ev.xclient.data.l[3] = l3;
        ev.xclient.data.l[4] = l4;
        XSendEvent(dpy, target, False, NoEventMask, &ev);
    }

    bool drag_image(WinKind source, const Image& img) override {
        if (!img.valid()) return false;
        forget_drop_file();
        if (!img.encode_png(drag_png)) return false;

        char tmpl[] = "/tmp/Screenshot-XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) return false;
        if (write(fd, drag_png.data(), drag_png.size()) != ssize_t(drag_png.size())) {
            close(fd);
            unlink(tmpl);
            return false;
        }
        fsync(fd);
        close(fd);
        std::string png_path = std::string(tmpl) + ".png";
        if (rename(tmpl, png_path.c_str()) != 0) {
            unlink(tmpl);
            return false;
        }
        chmod(png_path.c_str(), 0644);
        drag_path = png_path;
        drag_uri_plain = file_uri_from_path(drag_path);
        drag_uri = drag_uri_plain + "\r\n";
        has_drag = true;

        Window srcw = wins[idx(source)].win;
        if (!srcw) srcw = wins[idx(WinKind::Main)].win;
        if (!srcw) {
            forget_drop_file();
            return false;
        }

        Atom types[7] = {uri_list, text_plain, text_plain_utf8, utf8_string, moz_url, XA_STRING, png_atom};
        XChangeProperty(dpy, srcw, xdnd_typelist, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(types), 7);
        XSetSelectionOwner(dpy, xdnd_selection, srcw, CurrentTime);
        XGrabPointer(dpy, srcw, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                     GrabModeAsync, None, cursors[int(CursorType::Hand)], CurrentTime);
        XFlush(dpy);

        Window target = None;
        bool accepted = false;
        bool dropped = false;
        bool done = false;
        int last_tx = -1, last_ty = -1;
        Time drop_time = CurrentTime;

        while (!done) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == SelectionRequest) {
                serve_clipboard(&ev.xselectionrequest);
                continue;
            }
            if (ev.type == KeyPress) {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape) {
                    if (target) send_xdnd(target, xdnd_leave, long(srcw), 0, 0, 0, 0);
                    done = true;
                    dropped = false;
                    continue;
                }
            }
            if (ev.type == ButtonRelease || (ev.type == ClientMessage && ev.xclient.message_type == xdnd_finished)) {
                if (ev.type == ClientMessage) {
                    dropped = accepted;
                    done = true;
                    continue;
                }
                drop_time = ev.xbutton.time;
                if (target && accepted) {
                    send_xdnd(target, xdnd_drop, long(srcw), 0, long(drop_time), 0, 0);
                    XFlush(dpy);
                    uint64_t start = ticks_ms();
                    while (ticks_ms() - start < 4000) {
                        if (!XPending(dpy)) {
                            wait_ms(10);
                            continue;
                        }
                        XEvent e2;
                        XNextEvent(dpy, &e2);
                        if (e2.type == SelectionRequest) serve_clipboard(&e2.xselectionrequest);
                        if (e2.type == ClientMessage && e2.xclient.message_type == xdnd_finished) break;
                    }
                    dropped = true;
                } else if (target) {
                    send_xdnd(target, xdnd_leave, long(srcw), 0, 0, 0, 0);
                }
                done = true;
                continue;
            }
            if (ev.type == ClientMessage && ev.xclient.message_type == xdnd_status) {
                accepted = (ev.xclient.data.l[1] & 1) != 0;
                continue;
            }
            int rx = 0, ry = 0;
            if (ev.type == MotionNotify) {
                rx = ev.xmotion.x_root;
                ry = ev.xmotion.y_root;
            } else {
                Window rr, cr;
                int wx, wy;
                unsigned m;
                XQueryPointer(dpy, root, &rr, &cr, &rx, &ry, &wx, &wy, &m);
            }
            Window nt = find_xdnd_target(rx, ry, srcw);
            if (nt != target) {
                if (target) send_xdnd(target, xdnd_leave, long(srcw), 0, 0, 0, 0);
                target = nt;
                accepted = false;
                if (target) {
                    // Bit 0: more than 3 types, use XdndTypeList. Prefer file URI over image bytes.
                    send_xdnd(target, xdnd_enter, long(srcw), (5L << 24) | 1L, long(uri_list), long(text_plain),
                              long(utf8_string));
                }
            }
            if (target && (rx != last_tx || ry != last_ty)) {
                last_tx = rx;
                last_ty = ry;
                send_xdnd(target, xdnd_position, long(srcw), 0, (long(rx) << 16) | (ry & 0xffff), long(CurrentTime),
                          long(xdnd_action_copy));
            }
        }

        XUngrabPointer(dpy, CurrentTime);
        XFlush(dpy);
        // Keep the PNG on disk so the drop target can open it after we return.
        return dropped;
    }

    std::string exe_dir() override { return exe; }
};

}  // namespace

Platform* create_platform() { return new X11Platform(); }
