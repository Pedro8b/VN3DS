// Shared app context and small UI drawing helpers.
#pragma once
#include <3ds.h>
#include <citro2d.h>

#include <string>
#include <vector>

#include "font.h"
#include "gfx.h"

namespace ui {

// palette
extern const u32 BG, PANEL, PANEL_HI, ACCENT, TEXT, TEXT_DIM, TEXT_ERR, SHADOW;

struct Input {
    u32 down = 0, held = 0, up = 0;
    touchPosition touch{};
    bool touchDown = false, touchHeld = false, touchUp = false;
    int touchStartX = 0, touchStartY = 0, lastX = 0, lastY = 0;
    bool tapped = false;  // released without dragging
    int dragDY = 0;       // vertical movement this frame while held
    circlePosition circle{};
};

struct App {
    C3D_RenderTarget* top = nullptr;
    C3D_RenderTarget* topRight = nullptr;  // right eye (stereoscopic 3D)
    C3D_RenderTarget* bottom = nullptr;
    FontRenderer font;
    bool quit = false;
    bool n3ds = false;
    std::string fallbackFont = "romfs:/font.ttf";
};

bool hit(const Input& in, float x, float y, float w, float h);  // tapped inside rect
void text(App& app, const std::string& s, float x, float y, u32 color, int size = 0);
void textCentered(App& app, const std::string& s, float cx, float y, u32 color, int size = 0);
std::string ellipsize(App& app, const std::string& s, float maxW);
void button(App& app, float x, float y, float w, float h, const std::string& label, bool selected,
            bool enabled = true);
void hint(App& app, const std::string& s);  // bottom line of the bottom screen

// Generic vertical list menu (d-pad + touch). Returns chosen index or -1; -2 on B.
struct Menu {
    std::vector<std::string> items;
    std::vector<bool> enabled;
    int sel = 0;
    std::string title;
    int update(const Input& in, float x, float y, float w, float rowH);
    void draw(App& app, float x, float y, float w, float rowH);
};

}  // namespace ui
