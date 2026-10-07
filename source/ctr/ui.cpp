#include "ui.h"

namespace ui {

const u32 BG = C2D_Color32(18, 18, 26, 255);
const u32 PANEL = C2D_Color32(38, 40, 56, 235);
const u32 PANEL_HI = C2D_Color32(70, 92, 160, 255);
const u32 ACCENT = C2D_Color32(120, 170, 255, 255);
const u32 TEXT = C2D_Color32(240, 240, 245, 255);
const u32 TEXT_DIM = C2D_Color32(150, 152, 170, 255);
const u32 TEXT_ERR = C2D_Color32(255, 110, 110, 255);
const u32 SHADOW = C2D_Color32(0, 0, 0, 170);

bool hit(const Input& in, float x, float y, float w, float h) {
    return in.tapped && in.lastX >= x && in.lastX < x + w && in.lastY >= y && in.lastY < y + h;
}

void text(App& app, const std::string& s, float x, float y, u32 color, int size) {
    int old = app.font.size();
    if (size > 0) app.font.setSize(size);
    app.font.draw(s, x, y, color);
    if (size > 0) app.font.setSize(old);
}

void textCentered(App& app, const std::string& s, float cx, float y, u32 color, int size) {
    int old = app.font.size();
    if (size > 0) app.font.setSize(size);
    float w = app.font.measure(s);
    app.font.draw(s, cx - w / 2, y, color);
    if (size > 0) app.font.setSize(old);
}

std::string ellipsize(App& app, const std::string& s, float maxW) {
    if (app.font.measure(s) <= maxW) return s;
    std::string r = s;
    while (!r.empty() && app.font.measure(r + "…") > maxW) {
        r.pop_back();
        while (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0x80) r.pop_back();
        if (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0xC0) r.pop_back();
    }
    return r + "…";
}

void button(App& app, float x, float y, float w, float h, const std::string& label, bool selected, bool enabled) {
    gfx::drawRect(x, y, w, h, selected ? PANEL_HI : PANEL);
    if (selected) gfx::drawRect(x, y + h - 2, w, 2, ACCENT);
    float ty = y + (h - app.font.lineHeight()) / 2;
    std::string l = ellipsize(app, label, w - 12);
    app.font.draw(l, x + (w - app.font.measure(l)) / 2, ty, enabled ? TEXT : TEXT_DIM);
}

void hint(App& app, const std::string& s) {
    int old = app.font.size();
    app.font.setSize(11);
    gfx::drawRect(0, gfx::BOT_H - 16, gfx::BOT_W, 16, SHADOW);
    app.font.draw(ellipsize(app, s, gfx::BOT_W - 8), 4, gfx::BOT_H - 15, TEXT_DIM);
    app.font.setSize(old);
}

int Menu::update(const Input& in, float x, float y, float w, float rowH) {
    int n = (int)items.size();
    if (n == 0) return (in.down & KEY_B) ? -2 : -1;
    auto ok = [&](int i) { return i >= 0 && i < n && (enabled.empty() || enabled[i]); };
    if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) {
        for (int k = 1; k <= n; k++)
            if (ok((sel + k) % n)) {
                sel = (sel + k) % n;
                break;
            }
    }
    if (in.down & (KEY_UP | KEY_CPAD_UP)) {
        for (int k = 1; k <= n; k++)
            if (ok((sel - k + n) % n)) {
                sel = (sel - k + n) % n;
                break;
            }
    }
    if (in.down & KEY_A && ok(sel)) return sel;
    if (in.down & KEY_B) return -2;
    float top = y + (title.empty() ? 0 : rowH);
    for (int i = 0; i < n; i++)
        if (hit(in, x, top + i * rowH, w, rowH - 4) && ok(i)) {
            sel = i;
            return i;
        }
    return -1;
}

void Menu::draw(App& app, float x, float y, float w, float rowH) {
    float top = y;
    if (!title.empty()) {
        textCentered(app, title, x + w / 2, y + (rowH - app.font.lineHeight()) / 2, ACCENT);
        top += rowH;
    }
    for (int i = 0; i < (int)items.size(); i++)
        button(app, x, top + i * rowH, w, rowH - 4, items[i], i == sel, enabled.empty() || enabled[i]);
}

}  // namespace ui
