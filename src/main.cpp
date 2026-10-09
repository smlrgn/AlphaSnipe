#include "app.hpp"

#include <cstdio>
#include <memory>
#include <string>

#if defined(_WIN32) && defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int real_main(int argc, char** argv) {
    CliOptions opt;
    if (!parse_cli(argc, argv, opt)) return 2;
    if (opt.self_test) return run_self_test();

    std::unique_ptr<Platform> plat(create_platform());
    if (!plat->init()) {
        std::fprintf(stderr, "Failed to initialize display.\n");
        return 1;
    }
    Font::load_app_fonts(plat->exe_dir());

    if (opt.headless) {
        Image shot;
        if (opt.demo) {
            Rect vs = plat->virtual_screen();
            shot = Image::make_demo(std::max(640, vs.w), std::max(480, vs.h));
        } else {
            shot = plat->capture_screen();
        }
        if (!shot.valid()) {
            plat->shutdown();
            std::fprintf(stderr, "Capture failed.\n");
            return 1;
        }
        if (!opt.region.empty()) {
            shot = shot.crop(opt.region.x, opt.region.y, opt.region.w, opt.region.h);
        }
        std::vector<uint8_t> png;
        if (!shot.encode_png(png)) {
            plat->shutdown();
            std::fprintf(stderr, "PNG encode failed.\n");
            return 1;
        }
        auto slash = opt.output.find_last_of("/\\");
        if (slash != std::string::npos) plat->make_dirs(opt.output.substr(0, slash));
        if (!plat->write_file(opt.output, png.data(), png.size())) {
            plat->shutdown();
            std::fprintf(stderr, "Could not write %s\n", opt.output.c_str());
            return 1;
        }
        std::printf("Wrote %s (%d x %d, %zu bytes)\n", opt.output.c_str(), shot.w, shot.h, png.size());
        plat->shutdown();
        return 0;
    }

    int rc = run_app(plat.get(), opt);
    plat->shutdown();
    return rc;
}

#if defined(_WIN32) && defined(_MSC_VER)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return real_main(__argc, __argv); }
#else
int main(int argc, char** argv) { return real_main(argc, argv); }
#endif
