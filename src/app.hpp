#pragma once

#include "platform.hpp"
#include "font.hpp"

struct CliOptions {
    bool self_test = false;
    bool demo = false;
    bool start_new = false;
    bool headless = false;
    SnipMode mode = SnipMode::Rectangle;
    int delay = 0;
    std::string output;
    Rect region{};
};

int run_self_test();
int run_app(Platform* plat, const CliOptions& opt);
void print_help(const char* argv0);
bool parse_cli(int argc, char** argv, CliOptions& opt);
