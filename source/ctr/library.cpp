#include "library.h"

#include <algorithm>

#include "config.h"

static const float ROW_H = 44, LIST_Y = 4, LIST_H = gfx::BOT_H - 20;

void Library::refresh() {
    entries_ = vn::Novel::scan(g_config.allNovelDirs());
    icons_.clear();
    thumb_.reset();
    thumbFor_ = -1;
    sel_ = std::min(sel_, std::max(0, (int)entries_.size() - 1));
    scanned_ = true;
}

gfx::TexPtr Library::loadThumb(const vn::NovelEntry& e, const char* name, int maxW, int maxH) {
    std::vector<uint8_t> data;
    if (!vn::Novel::readRootFile(e, name, data)) return nullptr;
    vn::MemStream ms(std::move(data));
    vn::Image img;
    if (!vn::decodeImage(ms, img)) return nullptr;
    float s = std::min(maxW / (float)img.w, maxH / (float)img.h);
    int w = std::max(1, (int)(img.w * s + 0.5f)), h = std::max(1, (int)(img.h * s + 0.5f));
    auto t = std::make_shared<gfx::Texture>();
    if (!t->create(vn::scaleImage(img, w, h))) return nullptr;
    return t;
}

bool Library::update(const ui::Input& in, vn::NovelEntry& chosen) {
    if (!scanned_) refresh();
    int n = (int)entries_.size();
    if (in.down & KEY_Y) refresh();
    if (in.down & KEY_START) app_.quit = true;
    if (n == 0) return false;
    if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) sel_ = (sel_ + 1) % n;
    if (in.down & (KEY_UP | KEY_CPAD_UP)) sel_ = (sel_ - 1 + n) % n;
    if (in.down & KEY_RIGHT) sel_ = std::min(n - 1, sel_ + 4);
    if (in.down & KEY_LEFT) sel_ = std::max(0, sel_ - 4);
    // touch: drag scrolls, tap selects / opens
    if (in.touchHeld && in.dragDY) scroll_ -= in.dragDY;
    if (in.tapped && in.lastY < LIST_Y + LIST_H) {
        int i = (int)((in.lastY - LIST_Y + scroll_) / ROW_H);
        if (i >= 0 && i < n) {
            if (i == sel_) {
                chosen = entries_[sel_];
                return true;
            }
            sel_ = i;
        }
    }
    if (in.down & KEY_A) {
        chosen = entries_[sel_];
        return true;
    }
    // keep selection visible (unless dragging)
    if (!in.touchHeld) {
        float y = sel_ * ROW_H;
        if (y < scroll_) scroll_ = y;
        if (y + ROW_H > scroll_ + LIST_H) scroll_ = y + ROW_H - LIST_H;
    }
    scroll_ = std::max(0.0f, std::min(scroll_, std::max(0.0f, n * ROW_H - LIST_H)));
    return false;
}

void Library::drawTop() {
    gfx::drawRect(0, 0, gfx::TOP_W, gfx::TOP_H, ui::BG);
    ui::textCentered(app_, "VN3DS", gfx::TOP_W / 2, 6, ui::ACCENT, 18);
#ifdef APP_VERSION
    ui::textCentered(app_, "v" APP_VERSION, gfx::TOP_W - 24, 226, ui::TEXT_DIM, 10);
#endif
    if (entries_.empty()) {
        float y = 60;
        for (auto& l : {"No novels found.", "", "Copy novels to /vnds/novels/<name>/", "(classic VNDS, Vita/Higurashi-Vita and .zip",
                        "layouts are all detected automatically)", "", "Y: rescan    START: exit"}) {
            ui::textCentered(app_, l, gfx::TOP_W / 2, y, ui::TEXT_DIM, 14);
            y += 18;
        }
        return;
    }
    const auto& e = entries_[sel_];
    if (thumbFor_ != sel_) {
        thumb_ = loadThumb(e, "thumbnail.png", 256, 150);
        if (!thumb_) thumb_ = loadThumb(e, "thumbnail.jpg", 256, 150);
        thumbFor_ = sel_;
    }
    if (thumb_) {
        float x = (gfx::TOP_W - thumb_->width()) / 2.0f;
        gfx::drawRect(x - 2, 34, thumb_->width() + 4, thumb_->height() + 4, ui::PANEL);
        gfx::drawImage(*thumb_, x, 36);
    }
    float y = 36 + 154;
    ui::textCentered(app_, ui::ellipsize(app_, e.title, 380), gfx::TOP_W / 2, y, ui::TEXT, 16);
    ui::textCentered(app_, ui::ellipsize(app_, e.layout + "  ·  " + e.path, 380), gfx::TOP_W / 2, y + 22,
                     ui::TEXT_DIM, 11);
    if (!message_.empty()) {
        gfx::drawRect(0, gfx::TOP_H - 20, gfx::TOP_W, 20, ui::SHADOW);
        ui::textCentered(app_, ui::ellipsize(app_, message_, 390), gfx::TOP_W / 2, gfx::TOP_H - 18, ui::TEXT_ERR, 12);
    }
}

void Library::drawBottom() {
    gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, ui::BG);
    int n = (int)entries_.size();
    int first = std::max(0, (int)(scroll_ / ROW_H));
    int budget = 1;  // load at most one icon per frame to keep scrolling smooth
    for (int i = first; i < n && i * ROW_H - scroll_ < LIST_H; i++) {
        float y = LIST_Y + i * ROW_H - scroll_;
        const auto& e = entries_[i];
        gfx::drawRect(4, y, gfx::BOT_W - 8, ROW_H - 4, i == sel_ ? ui::PANEL_HI : ui::PANEL);
        auto it = icons_.find(e.path);
        if (it == icons_.end() && budget > 0) {
            budget--;
            it = icons_.emplace(e.path, loadThumb(e, "icon.png", 32, 32)).first;
        }
        if (it != icons_.end() && it->second) gfx::drawImage(*it->second, 10, y + 4);
        app_.font.setSize(14);
        app_.font.draw(ui::ellipsize(app_, e.title, gfx::BOT_W - 60), 48, y + 3, ui::TEXT);
        app_.font.setSize(11);
        app_.font.draw(e.layout, 48, y + 22, ui::TEXT_DIM);
    }
    app_.font.setSize(g_config.fontSize);
    ui::hint(app_, "A: play   Y: rescan   START: exit");
}
