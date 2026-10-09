#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "platform.hpp"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <ole2.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace {

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}
std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

KeyId map_vk(WPARAM vk, WPARAM key) {
    (void)key;
    switch (vk) {
        case VK_ESCAPE: return KeyId::Esc;
        case VK_RETURN: return KeyId::Return;
        case VK_SPACE: return KeyId::Space;
        case VK_BACK: return KeyId::Backspace;
        case VK_DELETE: return KeyId::Delete;
        case VK_TAB: return KeyId::Tab;
        case VK_LEFT: return KeyId::Left;
        case VK_RIGHT: return KeyId::Right;
        case VK_UP: return KeyId::Up;
        case VK_DOWN: return KeyId::Down;
        case VK_HOME: return KeyId::Home;
        case VK_END: return KeyId::End;
        case 'A': return KeyId::A;
        case 'C': return KeyId::C;
        case 'D': return KeyId::D;
        case 'E': return KeyId::E;
        case 'H': return KeyId::H;
        case 'N': return KeyId::N;
        case 'O': return KeyId::O;
        case 'P': return KeyId::P;
        case 'S': return KeyId::S;
        case 'V': return KeyId::V;
        case 'X': return KeyId::X;
        case 'Y': return KeyId::Y;
        case 'Z': return KeyId::Z;
        case '1':
        case VK_NUMPAD1: return KeyId::Num1;
        case '2':
        case VK_NUMPAD2: return KeyId::Num2;
        case '3':
        case VK_NUMPAD3: return KeyId::Num3;
        case '4':
        case VK_NUMPAD4: return KeyId::Num4;
        case VK_F1: return KeyId::F1;
        default: return KeyId::Unknown;
    }
}

struct Wnd {
    HWND hwnd = nullptr;
    int w = 0, h = 0;
    Image last;
    HDC memdc = nullptr;
    HBITMAP dib = nullptr;
    void* bits = nullptr;
};

static const wchar_t* kClass = L"AlphaSnipeWindow";
static const UINT kHotkeyId = 0x5331;

HGLOBAL dup_hglobal(HGLOBAL src) {
    if (!src) return nullptr;
    SIZE_T n = GlobalSize(src);
    HGLOBAL dst = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!dst) return nullptr;
    void* s = GlobalLock(src);
    void* d = GlobalLock(dst);
    if (s && d) memcpy(d, s, n);
    if (s) GlobalUnlock(src);
    if (d) GlobalUnlock(dst);
    return dst;
}

class EnumFormats final : public IEnumFORMATETC {
public:
    FORMATETC fmt[5]{};
    int n = 0, i = 0;
    ULONG ref = 1;
    EnumFormats(FORMATETC* f, int count, int start = 0) {
        n = count;
        i = start;
        for (int k = 0; k < count; ++k) fmt[k] = f[k];
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *pp = this;
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt, ULONG* fetched) override {
        ULONG got = 0;
        while (got < celt && i < n) rgelt[got++] = fmt[i++];
        if (fetched) *fetched = got;
        return got == celt ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        i += int(celt);
        return i <= n ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        i = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** pp) override {
        *pp = new EnumFormats(fmt, n, i);
        return S_OK;
    }
};

class ImageDataObject final : public IDataObject {
public:
    ULONG ref = 1;
    HGLOBAL hdrop = nullptr;
    HGLOBAL hdib = nullptr;
    HGLOBAL hpng = nullptr;
    HGLOBAL hnamew = nullptr;
    UINT png_fmt = 0;
    CLIPFORMAT filenamew = 0;
    FORMATETC fmts[5]{};
    int nfmt = 0;

    ~ImageDataObject() {
        if (hdrop) GlobalFree(hdrop);
        if (hdib) GlobalFree(hdib);
        if (hpng) GlobalFree(hpng);
        if (hnamew) GlobalFree(hnamew);
    }

    void add_fmt(CLIPFORMAT cf, DWORD tymed) {
        if (nfmt >= 5) return;
        FORMATETC f{};
        f.cfFormat = cf;
        f.dwAspect = DVASPECT_CONTENT;
        f.lindex = -1;
        f.tymed = tymed;
        fmts[nfmt++] = f;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *pp = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fmt, STGMEDIUM* med) override {
        if (!fmt || !med) return E_INVALIDARG;
        ZeroMemory(med, sizeof(*med));
        HGLOBAL src = nullptr;
        if (fmt->cfFormat == CF_HDROP && hdrop && (fmt->tymed & TYMED_HGLOBAL)) src = hdrop;
        else if (fmt->cfFormat == CF_DIB && hdib && (fmt->tymed & TYMED_HGLOBAL)) src = hdib;
        else if (filenamew && fmt->cfFormat == filenamew && hnamew && (fmt->tymed & TYMED_HGLOBAL)) src = hnamew;
        else if (png_fmt && fmt->cfFormat == CLIPFORMAT(png_fmt) && hpng && (fmt->tymed & TYMED_HGLOBAL)) src = hpng;
        else return DV_E_FORMATETC;
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = dup_hglobal(src);
        med->pUnkForRelease = nullptr;
        return med->hGlobal ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fmt) override {
        if (!fmt) return E_INVALIDARG;
        if (!(fmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (fmt->cfFormat == CF_HDROP && hdrop) return S_OK;
        if (fmt->cfFormat == CF_DIB && hdib) return S_OK;
        if (filenamew && fmt->cfFormat == filenamew && hnamew) return S_OK;
        if (png_fmt && fmt->cfFormat == CLIPFORMAT(png_fmt) && hpng) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** pp) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        *pp = new EnumFormats(fmts, nfmt);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

class DropSource final : public IDropSource {
public:
    ULONG ref = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *pp = this;
            AddRef();
            return S_OK;
        }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --ref;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

class Win32Platform;

static Win32Platform* g_plat = nullptr;

class Win32Platform final : public Platform {
public:
    HINSTANCE inst = nullptr;
    Wnd wins[4];
    std::vector<PlatEvent> queue;
    bool running = false;
    std::string exe;
    HCURSOR cursors[10]{};
    UINT png_fmt = 0;
    CLIPFORMAT filenamew_fmt = 0;
    std::wstring drop_keep;

    int idx(WinKind k) const { return int(k); }

    ~Win32Platform() override { shutdown(); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (g_plat) return g_plat->handle(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    WinKind kind_of(HWND hwnd) {
        for (int i = 0; i < 4; ++i)
            if (wins[i].hwnd == hwnd) return WinKind(i);
        return WinKind::Main;
    }

    void push(const PlatEvent& e) { queue.push_back(e); }

    LRESULT handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WinKind k = kind_of(hwnd);
        switch (msg) {
            case WM_CLOSE: {
                PlatEvent e;
                e.type = PlatEvent::Close;
                e.win = k;
                push(e);
                return 0;
            }
            case WM_SIZE: {
                PlatEvent e;
                e.type = PlatEvent::Resize;
                e.win = k;
                e.width = LOWORD(lParam);
                e.height = HIWORD(lParam);
                wins[idx(k)].w = e.width;
                wins[idx(k)].h = e.height;
                push(e);
                return 0;
            }
            case WM_MOUSEMOVE: {
                PlatEvent e;
                e.type = PlatEvent::MouseMove;
                e.win = k;
                e.x = GET_X_LPARAM(lParam);
                e.y = GET_Y_LPARAM(lParam);
                push(e);
                return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN: {
                SetCapture(hwnd);
                PlatEvent e;
                e.type = PlatEvent::MouseDown;
                e.win = k;
                e.x = GET_X_LPARAM(lParam);
                e.y = GET_Y_LPARAM(lParam);
                e.button = msg == WM_LBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2 : 3;
                push(e);
                return 0;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP: {
                ReleaseCapture();
                PlatEvent e;
                e.type = PlatEvent::MouseUp;
                e.win = k;
                e.x = GET_X_LPARAM(lParam);
                e.y = GET_Y_LPARAM(lParam);
                e.button = msg == WM_LBUTTONUP ? 1 : msg == WM_MBUTTONUP ? 2 : 3;
                push(e);
                return 0;
            }
            case WM_MOUSEWHEEL: {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                PlatEvent e;
                e.type = PlatEvent::Wheel;
                e.win = k;
                e.x = pt.x;
                e.y = pt.y;
                e.dy = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1 : -1;
                push(e);
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                PlatEvent e;
                e.type = PlatEvent::KeyDown;
                e.win = k;
                e.key = map_vk(wParam, lParam);
                if (GetKeyState(VK_SHIFT) & 0x8000) e.mods |= Mods::Shift;
                if (GetKeyState(VK_CONTROL) & 0x8000) e.mods |= Mods::Ctrl;
                if (GetKeyState(VK_MENU) & 0x8000) e.mods |= Mods::Alt;
                if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) e.mods |= Mods::Meta;
                push(e);
                return 0;
            }
            case WM_CHAR: {
                if (wParam >= 32) {
                    PlatEvent e;
                    e.type = PlatEvent::Char;
                    e.win = k;
                    e.ch = char32_t(wParam);
                    push(e);
                }
                return 0;
            }
            case WM_HOTKEY: {
                PlatEvent e;
                e.type = PlatEvent::Hotkey;
                e.win = WinKind::Main;
                push(e);
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                Wnd& rec = wins[idx(k)];
                if (rec.memdc && rec.dib) {
                    BitBlt(hdc, 0, 0, rec.w, rec.h, rec.memdc, 0, 0, SRCCOPY);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    bool init() override {
        g_plat = this;
        inst = GetModuleHandleW(nullptr);
        SetProcessDPIAware();

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = kClass;
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
        if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);

        cursors[int(CursorType::Arrow)] = LoadCursor(nullptr, IDC_ARROW);
        cursors[int(CursorType::Cross)] = LoadCursor(nullptr, IDC_CROSS);
        cursors[int(CursorType::Hand)] = LoadCursor(nullptr, IDC_HAND);
        cursors[int(CursorType::IBeam)] = LoadCursor(nullptr, IDC_IBEAM);
        cursors[int(CursorType::SizeAll)] = LoadCursor(nullptr, IDC_SIZEALL);
        cursors[int(CursorType::SizeNwse)] = LoadCursor(nullptr, IDC_SIZENWSE);
        cursors[int(CursorType::SizeNesw)] = LoadCursor(nullptr, IDC_SIZENESW);
        cursors[int(CursorType::SizeWe)] = LoadCursor(nullptr, IDC_SIZEWE);
        cursors[int(CursorType::SizeNs)] = LoadCursor(nullptr, IDC_SIZENS);
        cursors[int(CursorType::Hidden)] = nullptr;

        png_fmt = RegisterClipboardFormatW(L"PNG");
        filenamew_fmt = CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILENAMEW));
        OleInitialize(nullptr);

        wchar_t path[MAX_PATH];
        GetModuleFileNameW(inst, path, MAX_PATH);
        std::wstring wp = path;
        auto slash = wp.find_last_of(L"\\/");
        if (slash != std::wstring::npos) wp.resize(slash);
        exe = wide_to_utf8(wp);
        running = true;
        return true;
    }

    void forget_drop_file() {
        if (!drop_keep.empty()) {
            DeleteFileW(drop_keep.c_str());
            drop_keep.clear();
        }
    }

    void shutdown() override {
        forget_drop_file();
        for (int i = 0; i < 4; ++i) destroy_window(WinKind(i));
        if (g_plat == this) g_plat = nullptr;
        running = false;
        OleUninitialize();
    }

    Rect virtual_screen() override {
        return {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    }

    std::vector<MonitorInfo> monitors() override {
        std::vector<MonitorInfo> out;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
                auto* v = reinterpret_cast<std::vector<MonitorInfo>*>(lp);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(mon, &mi);
                MonitorInfo m;
                m.x = mi.rcMonitor.left;
                m.y = mi.rcMonitor.top;
                m.w = mi.rcMonitor.right - mi.rcMonitor.left;
                m.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
                m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                v->push_back(m);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&out));
        if (out.empty()) {
            Rect vs = virtual_screen();
            out.push_back({vs.x, vs.y, vs.w, vs.h, true});
        }
        return out;
    }

    void mouse_screen_pos(int& x, int& y) override {
        POINT p;
        GetCursorPos(&p);
        x = p.x;
        y = p.y;
    }

    Image capture_screen() override {
        bool vis[4]{};
        for (int i = 0; i < 4; ++i) {
            vis[i] = wins[i].hwnd && IsWindowVisible(wins[i].hwnd);
            if (vis[i]) ShowWindow(wins[i].hwnd, SW_HIDE);
        }
        Sleep(60);
        Rect vs = virtual_screen();
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, vs.w, vs.h);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, vs.w, vs.h, screen, vs.x, vs.y, SRCCOPY | CAPTUREBLT);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = vs.w;
        bi.bmiHeader.biHeight = -vs.h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        Image img(vs.w, vs.h);
        std::vector<uint8_t> bgra(size_t(vs.w) * size_t(vs.h) * 4);
        GetDIBits(mem, bmp, 0, vs.h, bgra.data(), &bi, DIB_RGB_COLORS);
        for (int i = 0; i < vs.w * vs.h; ++i) {
            img.px[size_t(i) * 4 + 0] = bgra[size_t(i) * 4 + 2];
            img.px[size_t(i) * 4 + 1] = bgra[size_t(i) * 4 + 1];
            img.px[size_t(i) * 4 + 2] = bgra[size_t(i) * 4 + 0];
            img.px[size_t(i) * 4 + 3] = 255;
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        for (int i = 0; i < 4; ++i)
            if (vis[i]) ShowWindow(wins[i].hwnd, SW_SHOW);
        return img;
    }

    std::vector<TopWindow> windows() override {
        std::vector<TopWindow> list;
        struct Ctx {
            std::vector<TopWindow>* list;
            HWND ours[4];
        } ctx;
        ctx.list = &list;
        for (int i = 0; i < 4; ++i) ctx.ours[i] = wins[i].hwnd;
        EnumWindows(
            [](HWND hwnd, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                if (!IsWindowVisible(hwnd)) return TRUE;
                for (int i = 0; i < 4; ++i)
                    if (c->ours[i] == hwnd) return TRUE;
                LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                if (ex & WS_EX_TOOLWINDOW) return TRUE;
                RECT rc{};
                if (!GetWindowRect(hwnd, &rc)) return TRUE;
                int w = rc.right - rc.left, h = rc.bottom - rc.top;
                if (w < 8 || h < 8) return TRUE;
                wchar_t title[512];
                GetWindowTextW(hwnd, title, 512);
                if (title[0] == 0) return TRUE;
                TopWindow t;
                t.x = rc.left;
                t.y = rc.top;
                t.w = w;
                t.h = h;
                t.title = wide_to_utf8(title);
                t.id = uintptr_t(hwnd);
                c->list->push_back(std::move(t));
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));
        return list;
    }

    bool create_window(const WindowDesc& d) override {
        destroy_window(d.kind);
        DWORD ex = WS_EX_APPWINDOW;
        DWORD style = WS_OVERLAPPEDWINDOW;
        if (d.borderless) {
            style = WS_POPUP;
            ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        }
        if (d.always_on_top) ex |= WS_EX_TOPMOST;
        if (!d.resizable && !d.borderless) {
            style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        }
        RECT r{d.x, d.y, d.x + d.w, d.y + d.h};
        if (!d.borderless) AdjustWindowRectEx(&r, style, FALSE, ex);
        HWND hwnd = CreateWindowExW(ex, kClass, utf8_to_wide(d.title).c_str(), style, r.left, r.top, r.right - r.left,
                                    r.bottom - r.top, nullptr, nullptr, inst, nullptr);
        if (!hwnd) return false;
        Wnd rec;
        rec.hwnd = hwnd;
        rec.w = d.w;
        rec.h = d.h;
        wins[idx(d.kind)] = rec;
        if (d.visible) ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void free_dib(Wnd& rec) {
        if (rec.dib) {
            SelectObject(rec.memdc, GetStockObject(NULL_BRUSH));
            DeleteObject(rec.dib);
            rec.dib = nullptr;
            rec.bits = nullptr;
        }
        if (rec.memdc) {
            DeleteDC(rec.memdc);
            rec.memdc = nullptr;
        }
    }

    void destroy_window(WinKind k) override {
        Wnd& rec = wins[idx(k)];
        if (rec.hwnd) {
            DestroyWindow(rec.hwnd);
            rec.hwnd = nullptr;
        }
        free_dib(rec);
        rec = {};
    }

    void show_window(WinKind k, bool show) override {
        Wnd& rec = wins[idx(k)];
        if (!rec.hwnd) return;
        ShowWindow(rec.hwnd, show ? SW_SHOW : SW_HIDE);
        if (show) SetForegroundWindow(rec.hwnd);
    }

    void set_title(WinKind k, const char* title) override {
        if (wins[idx(k)].hwnd) SetWindowTextW(wins[idx(k)].hwnd, utf8_to_wide(title).c_str());
    }

    void set_size(WinKind k, int w, int h) override {
        Wnd& rec = wins[idx(k)];
        if (!rec.hwnd) return;
        RECT wr{0, 0, w, h};
        DWORD style = (DWORD)GetWindowLongPtrW(rec.hwnd, GWL_STYLE);
        DWORD ex = (DWORD)GetWindowLongPtrW(rec.hwnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&wr, style, FALSE, ex);
        SetWindowPos(rec.hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
        rec.w = w;
        rec.h = h;
    }

    void get_size(WinKind k, int& w, int& h) override {
        w = wins[idx(k)].w;
        h = wins[idx(k)].h;
        if (wins[idx(k)].hwnd) {
            RECT rc;
            GetClientRect(wins[idx(k)].hwnd, &rc);
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }
    }

    void present(WinKind k, const Image& img) override {
        Wnd& rec = wins[idx(k)];
        if (!rec.hwnd || !img.valid()) return;
        rec.last = img;
        rec.w = img.w;
        rec.h = img.h;
        if (!rec.memdc) rec.memdc = CreateCompatibleDC(nullptr);
        if (rec.dib) {
            DeleteObject(rec.dib);
            rec.dib = nullptr;
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = img.w;
        bi.bmiHeader.biHeight = -img.h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        rec.dib = CreateDIBSection(rec.memdc, &bi, DIB_RGB_COLORS, &rec.bits, nullptr, 0);
        if (!rec.bits) return;
        SelectObject(rec.memdc, rec.dib);
        auto* dst = static_cast<uint8_t*>(rec.bits);
        for (int i = 0; i < img.w * img.h; ++i) {
            dst[i * 4 + 0] = img.px[i * 4 + 2];
            dst[i * 4 + 1] = img.px[i * 4 + 1];
            dst[i * 4 + 2] = img.px[i * 4 + 0];
            dst[i * 4 + 3] = 255;
        }
        HDC hdc = GetDC(rec.hwnd);
        BitBlt(hdc, 0, 0, img.w, img.h, rec.memdc, 0, 0, SRCCOPY);
        ReleaseDC(rec.hwnd, hdc);
    }

    bool poll(PlatEvent& e) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                e.type = PlatEvent::Close;
                e.win = WinKind::Main;
                return true;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (queue.empty()) return false;
        e = queue.front();
        queue.erase(queue.begin());
        return true;
    }

    void wait_ms(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    uint64_t ticks_ms() override {
        using namespace std::chrono;
        return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    bool copy_image(const Image& img) override {
        std::vector<uint8_t> png;
        img.encode_png(png);
        int w = img.w, h = img.h;
        size_t dib_bytes = sizeof(BITMAPINFOHEADER) + size_t(w) * size_t(h) * 4;
        HGLOBAL hdib = GlobalAlloc(GMEM_MOVEABLE, dib_bytes);
        if (!hdib) return false;
        auto* bi = static_cast<BITMAPINFOHEADER*>(GlobalLock(hdib));
        ZeroMemory(bi, sizeof(*bi));
        bi->biSize = sizeof(BITMAPINFOHEADER);
        bi->biWidth = w;
        bi->biHeight = h;  // bottom-up DIB
        bi->biPlanes = 1;
        bi->biBitCount = 32;
        bi->biCompression = BI_RGB;
        auto* bits = reinterpret_cast<uint8_t*>(bi + 1);
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = img.row(h - 1 - y);
            uint8_t* d = bits + size_t(y) * size_t(w) * 4;
            for (int x = 0; x < w; ++x) {
                d[x * 4 + 0] = s[x * 4 + 2];
                d[x * 4 + 1] = s[x * 4 + 1];
                d[x * 4 + 2] = s[x * 4 + 0];
                d[x * 4 + 3] = s[x * 4 + 3];
            }
        }
        GlobalUnlock(hdib);

        HGLOBAL hpng = nullptr;
        if (!png.empty()) {
            hpng = GlobalAlloc(GMEM_MOVEABLE, png.size());
            if (hpng) {
                void* p = GlobalLock(hpng);
                memcpy(p, png.data(), png.size());
                GlobalUnlock(hpng);
            }
        }
        if (!OpenClipboard(wins[idx(WinKind::Main)].hwnd)) {
            GlobalFree(hdib);
            if (hpng) GlobalFree(hpng);
            return false;
        }
        EmptyClipboard();
        SetClipboardData(CF_DIB, hdib);
        if (hpng && png_fmt) SetClipboardData(png_fmt, hpng);
        CloseClipboard();
        return true;
    }

    std::string save_dialog(const std::string& suggested, const std::string& start_dir) override {
        wchar_t file[MAX_PATH];
        wcsncpy(file, utf8_to_wide(suggested).c_str(), MAX_PATH - 1);
        file[MAX_PATH - 1] = 0;
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = wins[idx(WinKind::Main)].hwnd;
        ofn.lpstrFilter = L"PNG image (*.png)\0*.png\0JPEG image (*.jpg)\0*.jpg\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        std::wstring dir = utf8_to_wide(start_dir);
        ofn.lpstrInitialDir = dir.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"png";
        if (!GetSaveFileNameW(&ofn)) return {};
        return wide_to_utf8(file);
    }

    std::string pictures_dir() override {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, path))) {
            return wide_to_utf8(path) + "\\Screenshots";
        }
        return exe + "\\Screenshots";
    }

    bool write_file(const std::string& path, const uint8_t* data, size_t n) override {
        HANDLE h = CreateFileW(utf8_to_wide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0;
        BOOL ok = WriteFile(h, data, DWORD(n), &wr, nullptr);
        CloseHandle(h);
        return ok && wr == n;
    }

    bool make_dirs(const std::string& path) override {
        std::wstring w = utf8_to_wide(path);
        for (size_t i = 0; i < w.size(); ++i) {
            if (w[i] == L'/' || w[i] == L'\\') {
                if (i > 2) {
                    wchar_t c = w[i];
                    w[i] = 0;
                    CreateDirectoryW(w.c_str(), nullptr);
                    w[i] = c;
                }
            }
        }
        CreateDirectoryW(w.c_str(), nullptr);
        return true;
    }

    void set_cursor(CursorType c) override { SetCursor(cursors[int(c)]); }

    void beep() override { MessageBeep(MB_OK); }

    bool register_hotkey() override {
        return RegisterHotKey(wins[idx(WinKind::Main)].hwnd, int(kHotkeyId), MOD_CONTROL | MOD_SHIFT, 'S') != 0;
    }

    bool drag_image(WinKind source, const Image& img) override {
        if (!img.valid()) return false;
        std::vector<uint8_t> png;
        if (!img.encode_png(png)) return false;

        forget_drop_file();
        wchar_t tmpdir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpdir);
        wchar_t tmp[MAX_PATH];
        wsprintfW(tmp, L"%sScreenshot-%u.png", tmpdir, GetTickCount());
        if (!write_file(wide_to_utf8(tmp), png.data(), png.size())) return false;
        drop_keep = tmp;

        auto* obj = new ImageDataObject();
        obj->png_fmt = png_fmt;
        obj->filenamew = filenamew_fmt;

        size_t nchars = wcslen(tmp) + 1;
        size_t path_bytes = (nchars + 1) * sizeof(wchar_t);
        obj->hdrop = GlobalAlloc(GHND, sizeof(DROPFILES) + path_bytes);
        if (obj->hdrop) {
            auto* df = static_cast<DROPFILES*>(GlobalLock(obj->hdrop));
            df->pFiles = sizeof(DROPFILES);
            df->fWide = TRUE;
            memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), tmp, nchars * sizeof(wchar_t));
            GlobalUnlock(obj->hdrop);
            obj->add_fmt(CF_HDROP, TYMED_HGLOBAL);
        }
        if (filenamew_fmt) {
            obj->hnamew = GlobalAlloc(GHND, nchars * sizeof(wchar_t));
            if (obj->hnamew) {
                void* p = GlobalLock(obj->hnamew);
                memcpy(p, tmp, nchars * sizeof(wchar_t));
                GlobalUnlock(obj->hnamew);
                obj->add_fmt(filenamew_fmt, TYMED_HGLOBAL);
            }
        }

        int w = img.w, h = img.h;
        size_t dib_bytes = sizeof(BITMAPINFOHEADER) + size_t(w) * size_t(h) * 4;
        obj->hdib = GlobalAlloc(GMEM_MOVEABLE, dib_bytes);
        if (obj->hdib) {
            auto* bi = static_cast<BITMAPINFOHEADER*>(GlobalLock(obj->hdib));
            ZeroMemory(bi, sizeof(*bi));
            bi->biSize = sizeof(BITMAPINFOHEADER);
            bi->biWidth = w;
            bi->biHeight = h;
            bi->biPlanes = 1;
            bi->biBitCount = 32;
            bi->biCompression = BI_RGB;
            auto* bits = reinterpret_cast<uint8_t*>(bi + 1);
            for (int y = 0; y < h; ++y) {
                const uint8_t* s = img.row(h - 1 - y);
                uint8_t* d = bits + size_t(y) * size_t(w) * 4;
                for (int x = 0; x < w; ++x) {
                    d[x * 4 + 0] = s[x * 4 + 2];
                    d[x * 4 + 1] = s[x * 4 + 1];
                    d[x * 4 + 2] = s[x * 4 + 0];
                    d[x * 4 + 3] = s[x * 4 + 3];
                }
            }
            GlobalUnlock(obj->hdib);
            obj->add_fmt(CF_DIB, TYMED_HGLOBAL);
        }

        if (png_fmt && !png.empty()) {
            obj->hpng = GlobalAlloc(GMEM_MOVEABLE, png.size());
            if (obj->hpng) {
                void* pbits = GlobalLock(obj->hpng);
                memcpy(pbits, png.data(), png.size());
                GlobalUnlock(obj->hpng);
                obj->add_fmt(CLIPFORMAT(png_fmt), TYMED_HGLOBAL);
            }
        }

        auto* src = new DropSource();
        DWORD effect = DROPEFFECT_COPY;
        HRESULT hr = DoDragDrop(obj, src, DROPEFFECT_COPY, &effect);
        src->Release();
        obj->Release();
        (void)source;
        return hr == DRAGDROP_S_DROP;
    }

    std::string exe_dir() override { return exe; }
};

}  // namespace

Platform* create_platform() { return new Win32Platform(); }
