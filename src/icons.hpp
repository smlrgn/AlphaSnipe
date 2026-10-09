#pragma once

#include "image.hpp"

enum class Icon {
    New,
    Rect,
    Freeform,
    Window,
    Full,
    Close,
    Pen,
    Highlighter,
    Eraser,
    Crop,
    Undo,
    Redo,
    Copy,
    Save,
    Check,
    Chevron,
    ZoomIn,
    ZoomOut,
    Scissors,
    DragDrop
};

void draw_icon(Image& dst, Rect r, Icon id, uint32_t color);
