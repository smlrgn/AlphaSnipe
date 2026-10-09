#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "platform.hpp"

#include <chrono>
#include <climits>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <libgen.h>
#include <mach-o/dyld.h>

namespace {

KeyId map_mac_key(unsigned short k) {
    switch (k) {
        case 53: return KeyId::Esc;
        case 36:
        case 76: return KeyId::Return;
        case 49: return KeyId::Space;
        case 51: return KeyId::Backspace;
        case 117: return KeyId::Delete;
        case 48: return KeyId::Tab;
        case 123: return KeyId::Left;
        case 124: return KeyId::Right;
        case 126: return KeyId::Up;
        case 125: return KeyId::Down;
        case 115: return KeyId::Home;
        case 119: return KeyId::End;
        case 0: return KeyId::A;
        case 8: return KeyId::C;
        case 2: return KeyId::D;
        case 14: return KeyId::E;
        case 4: return KeyId::H;
        case 45: return KeyId::N;
        case 31: return KeyId::O;
        case 35: return KeyId::P;
        case 1: return KeyId::S;
        case 9: return KeyId::V;
        case 7: return KeyId::X;
        case 16: return KeyId::Y;
        case 6: return KeyId::Z;
        case 18: return KeyId::Num1;
        case 19: return KeyId::Num2;
        case 20: return KeyId::Num3;
        case 21: return KeyId::Num4;
        case 122: return KeyId::F1;
        default: return KeyId::Unknown;
    }
}

struct MacWnd {
    NSWindow* window = nil;
    NSImageView* view = nil;
    int w = 0, h = 0;
    Image last;
};

class MacPlatform;

static MacPlatform* g_mac = nullptr;

}  // namespace

@interface SnipDragSource : NSObject <NSDraggingSource>
@property(nonatomic, assign) BOOL ended;
@property(nonatomic, assign) BOOL dropped;
@end

@implementation SnipDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    (void)session;
    (void)context;
    return NSDragOperationCopy;
}
- (void)draggingSession:(NSDraggingSession*)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation {
    (void)session;
    (void)screenPoint;
    self.dropped = operation != NSDragOperationNone;
    self.ended = YES;
}
@end

@interface SnipView : NSView
@property(nonatomic, assign) int kind;
@end

@implementation SnipView
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)isFlipped {
    return YES;
}
@end

namespace {

class MacPlatform final : public Platform {
public:
    MacWnd wins[4];
    std::vector<PlatEvent> queue;
    std::string exe;
    std::string drop_keep;
    NSCursor* last_cursor = nil;
    id hotkey_monitor = nil;

    int idx(WinKind k) const { return int(k); }

    ~MacPlatform() override { shutdown(); }

    bool init() override {
        g_mac = this;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        char buf[4096];
        uint32_t sz = sizeof(buf);
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            char resolved[4096];
            if (realpath(buf, resolved)) {
                exe = resolved;
                auto slash = exe.find_last_of('/');
                if (slash != std::string::npos) exe.resize(slash);
            }
        }
        if (exe.empty()) exe = ".";
        return true;
    }

    void forget_drop_file() {
        if (!drop_keep.empty()) {
            [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:drop_keep.c_str()] error:nil];
            drop_keep.clear();
        }
    }

    void shutdown() override {
        forget_drop_file();
        if (hotkey_monitor) {
            [NSEvent removeMonitor:hotkey_monitor];
            hotkey_monitor = nil;
        }
        for (int i = 0; i < 4; ++i) destroy_window(WinKind(i));
        g_mac = nullptr;
    }

    Rect virtual_screen() override {
        CGRect u = CGDisplayBounds(CGMainDisplayID());
        for (NSScreen* s in [NSScreen screens]) {
            NSRect f = [s frame];
            CGRect r = CGRectMake(f.origin.x, f.origin.y, f.size.width, f.size.height);
            u = CGRectUnion(u, r);
        }
        // Convert Cocoa bottom-left to top-left using union of screens in global cocoa space
        // Capture uses CG which is top-left. We'll treat 0,0 as minX, maxY flipped later.
        int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
        for (NSScreen* s in [NSScreen screens]) {
            NSRect f = [s frame];
            minx = std::min(minx, int(f.origin.x));
            miny = std::min(miny, int(f.origin.y));
            maxx = std::max(maxx, int(f.origin.x + f.size.width));
            maxy = std::max(maxy, int(f.origin.y + f.size.height));
        }
        if (minx == INT_MAX) {
            NSRect f = [[NSScreen mainScreen] frame];
            return {0, 0, int(f.size.width), int(f.size.height)};
        }
        return {minx, 0, maxx - minx, maxy - miny};
    }

    std::vector<MonitorInfo> monitors() override {
        std::vector<MonitorInfo> out;
        NSScreen* primary = [NSScreen mainScreen];
        Rect vs = virtual_screen();
        for (NSScreen* s in [NSScreen screens]) {
            NSRect f = [s frame];
            MonitorInfo m;
            m.x = int(f.origin.x - vs.x);
            // flip y: cocoa y-up from bottom of union
            CGFloat union_top = 0;
            for (NSScreen* t in [NSScreen screens]) {
                union_top = std::max(union_top, t.frame.origin.y + t.frame.size.height);
            }
            m.y = int(union_top - (f.origin.y + f.size.height));
            m.w = int(f.size.width);
            m.h = int(f.size.height);
            m.primary = (s == primary);
            out.push_back(m);
        }
        if (out.empty()) {
            Rect vs2 = virtual_screen();
            out.push_back({0, 0, vs2.w, vs2.h, true});
        }
        return out;
    }

    void mouse_screen_pos(int& x, int& y) override {
        NSPoint p = [NSEvent mouseLocation];
        Rect vs = virtual_screen();
        CGFloat union_top = 0;
        for (NSScreen* t in [NSScreen screens]) {
            union_top = std::max(union_top, t.frame.origin.y + t.frame.size.height);
        }
        x = int(p.x - vs.x);
        y = int(union_top - p.y);
    }

    Image capture_screen() override {
        for (int i = 0; i < 4; ++i)
            if (wins[i].window) [wins[i].window orderOut:nil];
        [NSApp hide:nil];
        wait_ms(80);

        CGRect bounds = CGDisplayBounds(CGMainDisplayID());
        for (NSScreen* s in [NSScreen screens]) {
            NSRect f = [s frame];
            CGFloat union_top = 0;
            for (NSScreen* t in [NSScreen screens])
                union_top = std::max(union_top, t.frame.origin.y + t.frame.size.height);
            CGRect r = CGRectMake(f.origin.x, union_top - (f.origin.y + f.size.height), f.size.width, f.size.height);
            bounds = CGRectUnion(bounds, r);
        }
        CGImageRef cg = CGWindowListCreateImage(CGRectInfinite, kCGWindowListOptionOnScreenOnly, kCGNullWindowID,
                                                kCGWindowImageDefault);
        Image out;
        if (cg) {
            size_t w = CGImageGetWidth(cg);
            size_t h = CGImageGetHeight(cg);
            out.resize(int(w), int(h));
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx =
                CGBitmapContextCreate(out.px.data(), w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
            CGContextRelease(ctx);
            CGColorSpaceRelease(cs);
            CGImageRelease(cg);
            // Ensure opaque alpha
            for (size_t i = 0; i < w * h; ++i)
                if (out.px[i * 4 + 3] == 0) out.px[i * 4 + 3] = 255;
        }
        [NSApp unhide:nil];
        for (int i = 0; i < 4; ++i)
            if (wins[i].window) [wins[i].window orderFront:nil];
        return out;
    }

    std::vector<TopWindow> windows() override {
        std::vector<TopWindow> list;
        CFArrayRef arr = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                    kCGNullWindowID);
        if (!arr) return list;
        CFIndex n = CFArrayGetCount(arr);
        for (CFIndex i = 0; i < n; ++i) {
            auto* info = (NSDictionary*)CFArrayGetValueAtIndex(arr, i);
            NSString* name = info[(id)kCGWindowName];
            NSString* owner = info[(id)kCGWindowOwnerName];
            NSDictionary* b = info[(id)kCGWindowBounds];
            if (!b) continue;
            CGRect r;
            CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)b, &r);
            if (r.size.width < 8 || r.size.height < 8) continue;
            NSNumber* layer = info[(id)kCGWindowLayer];
            if (layer && layer.intValue != 0) continue;
            TopWindow t;
            t.x = int(r.origin.x);
            t.y = int(r.origin.y);
            t.w = int(r.size.width);
            t.h = int(r.size.height);
            std::string title;
            if (name && name.length) title = name.UTF8String;
            else if (owner) title = owner.UTF8String;
            t.title = title;
            t.id = [info[(id)kCGWindowNumber] unsignedLongValue];
            if (title.find("AlphaSnipe") != std::string::npos) continue;
            list.push_back(std::move(t));
        }
        CFRelease(arr);
        return list;
    }

    bool create_window(const WindowDesc& d) override {
        destroy_window(d.kind);
        NSRect frame = NSMakeRect(d.x, d.y, d.w, d.h);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (d.resizable) style |= NSWindowStyleMaskResizable;
        if (d.borderless) style = NSWindowStyleMaskBorderless;
        NSWindow* w = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:style
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        [w setTitle:[NSString stringWithUTF8String:d.title]];
        [w setReleasedWhenClosed:NO];
        if (d.always_on_top) [w setLevel:NSScreenSaverWindowLevel];
        if (d.borderless) {
            [w setOpaque:YES];
            [w setHasShadow:NO];
            [w setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary];
        }
        SnipView* view = [[SnipView alloc] initWithFrame:NSMakeRect(0, 0, d.w, d.h)];
        view.kind = int(d.kind);
        NSImageView* iv = [[NSImageView alloc] initWithFrame:view.bounds];
        [iv setImageScaling:NSImageScaleNone];
        [iv setWantsLayer:YES];
        [view addSubview:iv];
        [w setContentView:view];
        MacWnd rec;
        rec.window = w;
        rec.view = iv;
        rec.w = d.w;
        rec.h = d.h;
        wins[idx(d.kind)] = rec;
        if (d.visible) {
            [w makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
        return true;
    }

    void destroy_window(WinKind k) override {
        MacWnd& rec = wins[idx(k)];
        if (rec.window) {
            [rec.window close];
            rec.window = nil;
            rec.view = nil;
        }
        rec = {};
    }

    void show_window(WinKind k, bool show) override {
        MacWnd& rec = wins[idx(k)];
        if (!rec.window) return;
        if (show) {
            [rec.window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        } else {
            [rec.window orderOut:nil];
        }
    }

    void set_title(WinKind k, const char* title) override {
        if (wins[idx(k)].window) [wins[idx(k)].window setTitle:[NSString stringWithUTF8String:title]];
    }

    void set_size(WinKind k, int w, int h) override {
        MacWnd& rec = wins[idx(k)];
        if (!rec.window) return;
        NSRect f = [rec.window contentRectForFrameRect:rec.window.frame];
        f.size = NSMakeSize(w, h);
        [rec.window setFrame:[rec.window frameRectForContentRect:f] display:YES];
        rec.w = w;
        rec.h = h;
    }

    void get_size(WinKind k, int& w, int& h) override {
        MacWnd& rec = wins[idx(k)];
        if (rec.window) {
            NSRect r = [rec.window contentRectForFrameRect:rec.window.frame];
            w = int(r.size.width);
            h = int(r.size.height);
            rec.w = w;
            rec.h = h;
        } else {
            w = rec.w;
            h = rec.h;
        }
    }

    void present(WinKind k, const Image& img) override {
        MacWnd& rec = wins[idx(k)];
        if (!rec.window || !img.valid()) return;
        rec.last = img;
        rec.w = img.w;
        rec.h = img.h;
        NSBitmapImageRep* rep =
            [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                    pixelsWide:img.w
                                                    pixelsHigh:img.h
                                                 bitsPerSample:8
                                               samplesPerPixel:4
                                                      hasAlpha:YES
                                                      isPlanar:NO
                                                colorSpaceName:NSCalibratedRGBColorSpace
                                                   bytesPerRow:img.w * 4
                                                  bitsPerPixel:32];
        memcpy([rep bitmapData], img.px.data(), size_t(img.w) * size_t(img.h) * 4);
        NSImage* nsimg = [[NSImage alloc] initWithSize:NSMakeSize(img.w, img.h)];
        [nsimg addRepresentation:rep];
        rec.view.frame = NSMakeRect(0, 0, img.w, img.h);
        rec.view.image = nsimg;
        [rec.view setNeedsDisplay:YES];
    }

    int mods_from(NSEvent* ev) {
        int m = 0;
        NSEventModifierFlags f = ev.modifierFlags;
        if (f & NSEventModifierFlagShift) m |= Mods::Shift;
        if (f & NSEventModifierFlagControl) m |= Mods::Ctrl;
        if (f & NSEventModifierFlagOption) m |= Mods::Alt;
        if (f & NSEventModifierFlagCommand) m |= Mods::Meta;
        return m;
    }

    NSPoint local(NSEvent* ev, WinKind k) {
        NSPoint p = [ev locationInWindow];
        NSView* v = wins[idx(k)].window.contentView;
        p = [v convertPoint:p fromView:nil];
        return p;
    }

    bool poll(PlatEvent& e) override {
        e = {};
        NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate distantPast]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (ev) {
            WinKind k = WinKind::Main;
            NSWindow* w = ev.window;
            for (int i = 0; i < 4; ++i)
                if (wins[i].window == w) k = WinKind(i);
            switch (ev.type) {
                case NSEventTypeLeftMouseDown:
                case NSEventTypeRightMouseDown:
                case NSEventTypeOtherMouseDown: {
                    NSPoint p = local(ev, k);
                    e.type = PlatEvent::MouseDown;
                    e.win = k;
                    e.x = int(p.x);
                    e.y = int(p.y);
                    e.button = ev.type == NSEventTypeLeftMouseDown ? 1 : ev.type == NSEventTypeOtherMouseDown ? 2 : 3;
                    return true;
                }
                case NSEventTypeLeftMouseUp:
                case NSEventTypeRightMouseUp:
                case NSEventTypeOtherMouseUp: {
                    NSPoint p = local(ev, k);
                    e.type = PlatEvent::MouseUp;
                    e.win = k;
                    e.x = int(p.x);
                    e.y = int(p.y);
                    e.button = ev.type == NSEventTypeLeftMouseUp ? 1 : ev.type == NSEventTypeOtherMouseUp ? 2 : 3;
                    return true;
                }
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged: {
                    NSPoint p = local(ev, k);
                    e.type = PlatEvent::MouseMove;
                    e.win = k;
                    e.x = int(p.x);
                    e.y = int(p.y);
                    return true;
                }
                case NSEventTypeScrollWheel: {
                    NSPoint p = local(ev, k);
                    e.type = PlatEvent::Wheel;
                    e.win = k;
                    e.x = int(p.x);
                    e.y = int(p.y);
                    e.dy = ev.scrollingDeltaY > 0 ? 1 : -1;
                    return true;
                }
                case NSEventTypeKeyDown: {
                    e.type = PlatEvent::KeyDown;
                    e.win = k;
                    e.key = map_mac_key(ev.keyCode);
                    e.mods = mods_from(ev);
                    NSString* ch = ev.charactersIgnoringModifiers;
                    if (ch.length) e.ch = [ch characterAtIndex:0];
                    if (e.ch == 0x1b) e.key = KeyId::Esc;
                    return true;
                }
                case NSEventTypeKeyUp: {
                    e.type = PlatEvent::KeyUp;
                    e.win = k;
                    e.key = map_mac_key(ev.keyCode);
                    e.mods = mods_from(ev);
                    return true;
                }
                default:
                    [NSApp sendEvent:ev];
                    break;
            }
        }
        if (!queue.empty()) {
            e = queue.front();
            queue.erase(queue.begin());
            return true;
        }
        // Close detection
        for (int i = 0; i < 4; ++i) {
            if (wins[i].window && !wins[i].window.isVisible && wins[i].last.valid()) {
                // ignore
            }
        }
        return false;
    }

    void wait_ms(int ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    uint64_t ticks_ms() override {
        using namespace std::chrono;
        return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    bool copy_image(const Image& img) override {
        std::vector<uint8_t> png;
        if (!img.encode_png(png)) return false;
        NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
        NSImage* nsimg = [[NSImage alloc] initWithData:data];
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return [pb writeObjects:@[ nsimg ]];
    }

    std::string save_dialog(const std::string& suggested, const std::string& start_dir) override {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setAllowedFileTypes:@[ @"png", @"jpg" ]];
        [panel setNameFieldStringValue:[NSString stringWithUTF8String:suggested.c_str()]];
        [panel setDirectoryURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:start_dir.c_str()]]];
        if ([panel runModal] != NSModalResponseOK) return {};
        return std::string(panel.URL.path.UTF8String);
    }

    std::string pictures_dir() override {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSPicturesDirectory, NSUserDomainMask, YES);
        std::string pic = paths.count ? std::string([paths[0] UTF8String]) : std::string(getenv("HOME") ? getenv("HOME") : ".");
        return pic + "/Screenshots";
    }

    bool write_file(const std::string& path, const uint8_t* data, size_t n) override {
        NSData* d = [NSData dataWithBytes:data length:n];
        return [d writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
    }

    bool make_dirs(const std::string& path) override {
        NSError* err = nil;
        [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:path.c_str()]
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:&err];
        return err == nil;
    }

    void set_cursor(CursorType c) override {
        switch (c) {
            case CursorType::Cross: [[NSCursor crosshairCursor] set]; break;
            case CursorType::Hand: [[NSCursor pointingHandCursor] set]; break;
            case CursorType::IBeam: [[NSCursor IBeamCursor] set]; break;
            case CursorType::SizeAll: [[NSCursor openHandCursor] set]; break;
            case CursorType::Hidden: [NSCursor hide]; break;
            default: [[NSCursor arrowCursor] set]; break;
        }
    }

    void beep() override { NSBeep(); }

    bool drag_image(WinKind source, const Image& img) override {
        if (!img.valid()) return false;
        std::vector<uint8_t> png;
        if (!img.encode_png(png)) return false;
        forget_drop_file();
        NSString* dir = NSTemporaryDirectory();
        NSString* path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"Screenshot-%u.png",
                                                                        arc4random()]];
        NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
        if (![data writeToFile:path atomically:YES]) return false;
        drop_keep = path.UTF8String;

        NSImage* nsimg = [[NSImage alloc] initWithData:data];
        NSURL* url = [NSURL fileURLWithPath:path];
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        [item setData:data forType:NSPasteboardTypePNG];
        [item setString:url.absoluteString forType:NSPasteboardTypeFileURL];

        NSDraggingItem* drag = [[NSDraggingItem alloc] initWithPasteboardWriter:item];
        MacWnd& rec = wins[idx(source)];
        if (!rec.window) rec = wins[idx(WinKind::Main)];
        if (!rec.window) return false;
        NSPoint loc = [rec.window mouseLocationOutsideOfEventStream];
        NSSize thumb = nsimg.size;
        if (thumb.width > 96 || thumb.height > 96) {
            float s = 96.f / float(std::max(thumb.width, thumb.height));
            thumb = NSMakeSize(thumb.width * s, thumb.height * s);
        }
        [drag setDraggingFrame:NSMakeRect(loc.x - thumb.width / 2, loc.y - thumb.height / 2, thumb.width, thumb.height)
                      contents:nsimg];

        NSEvent* ev = [NSApp currentEvent];
        if (!ev) {
            ev = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
                                    location:loc
                               modifierFlags:0
                                   timestamp:[[NSProcessInfo processInfo] systemUptime]
                                windowNumber:rec.window.windowNumber
                                     context:nil
                                 eventNumber:0
                                  clickCount:1
                                    pressure:1];
        }
        SnipDragSource* src = [[SnipDragSource alloc] init];
        [rec.window.contentView beginDraggingSessionWithItems:@[ drag ] event:ev source:src];
        while (!src.ended) {
            NSEvent* next = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES];
            if (next) [NSApp sendEvent:next];
        }
        return src.dropped;
    }

    bool register_hotkey() override {
        hotkey_monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                                handler:^(NSEvent* ev) {
                                                                  if ((ev.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask) ==
                                                                          (NSEventModifierFlagControl | NSEventModifierFlagShift) &&
                                                                      ev.keyCode == 1) {
                                                                      PlatEvent e;
                                                                      e.type = PlatEvent::Hotkey;
                                                                      queue.push_back(e);
                                                                  }
                                                                }];
        return true;
    }

    std::string exe_dir() override { return exe; }
};

}  // namespace

Platform* create_platform() { return new MacPlatform(); }
