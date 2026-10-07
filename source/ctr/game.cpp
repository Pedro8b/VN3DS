#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

#include "../core/higurashi.h"
#include "../core/save.h"
#include "audio.h"
#include "config.h"

using vn::Engine;

static const int NUM_SAVE_SLOTS = 30;
static const int QUICK_SLOT = 99;
static const int AUTO_SLOT = 98;
static const int LOAD_ENTRIES = NUM_SAVE_SLOTS + 2;  // + quick + auto
static const int AUTOSAVE_FRAMES = 60 * 120;           // every 2 minutes of play
static const float TEXT_X = 8, TEXT_Y = 6, STATUS_H = 16;
static const float REVEAL_SPEED[6] = {0.35f, 0.7f, 1.2f, 2.0f, 4.0f, 1e9f};
static const u32 TEXT_OLD = C2D_Color32(185, 188, 205, 255);

enum MenuId { M_RETURN, M_SAVE, M_LOAD, M_CHAPTERS, M_AUTO, M_SKIP, M_SETTINGS, M_RESTART, M_EXIT };

Game::~Game() {
    if (eng_ && !ended_ && g_config.autoSave) doSave(AUTO_SLOT, true);
    audio::stopAll();
    app_.font.setPrimary({});
    app_.font.setSize(g_config.fontSize);
}

bool Game::open(const vn::NovelEntry& e, std::string& err) {
    if (!novel_.open(e.path)) {
        err = "Cannot open " + e.path;
        return false;
    }
    vn::logf(vn::LOG_INFO, "sources:\n%s", novel_.describe().c_str());
    stageW_ = novel_.baseWidth();
    stageH_ = novel_.baseHeight();
    layout_ = g_config.screenLayout;
    loadNovelPrefs();
    computeGeometry();
    std::vector<uint8_t> ttf;
    if (auto s = novel_.openRoot("default.ttf")) s->readAll(ttf);
    app_.font.setPrimary(std::move(ttf));
    app_.font.setSize(g_config.fontSize);
    fontSizeUsed_ = g_config.fontSize;
    applyAudioSettings();

    if (novel_.engine() == vn::ENGINE_HIGURASHI) eng_.reset(new vn::HigurashiEngine(novel_, *this));
    else eng_.reset(new vn::ScriptEngine(novel_, *this));
    vn::readGlobals(vn::joinPath(novel_.saveDir(), "global.sav"), eng_->globals());
    if (!eng_->start()) {
        err = novel_.engine() == vn::ENGINE_HIGURASHI ? "No playable scripts in " + e.path
                                                      : novel_.mainScript() + " not found in " + e.path;
        return false;
    }
    if (g_config.resumeLast) {
        int slot = newestSave();
        if (slot >= 0) {
            doLoad(slot);
            toast(slot == AUTO_SLOT ? "Resumed from autosave" : slot == QUICK_SLOT ? "Resumed from quick save"
                                                                                   : "Resumed from slot " + std::to_string(slot + 1));
        }
    }
    return true;
}

void Game::applyAudioSettings() {
    audio::setCategoryVolumes(g_config.musicVolume / 100.0f, g_config.soundVolume / 100.0f);
}

// Per-novel display options (the current value; saves carry their own copy).
void Game::loadNovelPrefs() {
    vn::Ini ini;
    if (ini.load(vn::joinPath(novel_.saveDir(), "vn3ds.ini"))) pinSprites_ = ini.getInt("pinsprites", 0) != 0;
}

void Game::saveNovelPrefs() {
    std::string s = std::string("pinsprites=") + (pinSprites_ ? "1" : "0") + "\n";
    vn::makeDirs(novel_.saveDir());
    vn::writeWholeFile(vn::joinPath(novel_.saveDir(), "vn3ds.ini"), s.data(), s.size());
}

// ---------------------------------------------------------------------------
// Graphics

void Game::computeGeometry() {
    if (!novel_.hasImgIni() && refW_ && stageW_ == 256 && stageH_ == 192 && (refW_ != 256 || refH_ != 192)) {
        // No img.ini: coordinates are in the backgrounds' own resolution.
        stageW_ = refW_;
        stageH_ = refH_;
    }
    float aw = refW_ ? (float)refW_ : (float)stageW_;
    float ah = refH_ ? (float)refH_ : (float)stageH_;
    fitUsed_ = g_config.fitMode;
    picW_ = picOnTop() ? (float)gfx::TOP_W : (float)gfx::BOT_W;
    picH_ = picOnTop() ? (float)gfx::TOP_H : (float)gfx::BOT_H;
    float sx = picW_ / aw, sy = picH_ / ah;
    if (fitUsed_ == 0) sx = sy = std::min(sx, sy);       // fit: whole picture, black bars
    else if (fitUsed_ == 1) sx = sy = std::max(sx, sy);  // zoom: fill the screen, crop the edges
    // fitUsed_ == 2: stretch to the whole screen
    dispW_ = std::floor(aw * sx + 0.5f);
    dispH_ = std::floor(ah * sy + 0.5f);
    dispX_ = std::floor((picW_ - dispW_) / 2);
    dispY_ = std::floor((picH_ - dispH_) / 2);
    posSX_ = dispW_ / stageW_;
    posSY_ = dispH_ / stageH_;
    spriteKX_ = dispW_ / aw;
    spriteKY_ = dispH_ / ah;
    texCache_.clear();
    for (Scene* sc : {&shown_, &next_, &prev_, &preview_}) {
        sc->bgLoaded = false;
        sc->bgTex.reset();
        for (auto& l : sc->sprites) {
            l.loaded = false;
            l.tex.reset();
        }
    }
}

gfx::TexPtr Game::loadTexture(vn::ResCat cat, const std::string& path, bool isBg, bool fullIfTiny) {
    if (path.empty() || path == "~") return nullptr;
    std::string key = (isBg ? "B:" : fullIfTiny ? "H:" : "F:") + vn::toLower(path);
    for (auto it = texCache_.begin(); it != texCache_.end(); ++it) {
        if (it->key == key) {
            texCache_.splice(texCache_.begin(), texCache_, it);
            return it->tex;
        }
    }
    gfx::TexPtr tex;
    std::string resolved;
    auto s = novel_.openRes(cat, path, &resolved);
    if (s) {
        vn::Image img;
        if (vn::decodeImage(*s, img)) {
            s.reset();
            if (isBg && refW_ == 0 && img.w >= 64 && img.h >= 64) {
                refW_ = img.w;
                refH_ = img.h;
                computeGeometry();
                vn::logf(vn::LOG_INFO, "stage %dx%d, art %dx%d -> display %.0fx%.0f", stageW_, stageH_, refW_, refH_,
                         dispW_, dispH_);
            }
            int tw, th;
            if (isBg || (fullIfTiny && img.w <= 64 && img.h <= 64)) {
                tw = (int)dispW_;
                th = (int)dispH_;
            } else {
                tw = (int)std::floor(img.w * spriteKX_ + 0.5f);
                th = (int)std::floor(img.h * spriteKY_ + 0.5f);
            }
            tw = std::max(1, std::min(1024, tw));
            th = std::max(1, std::min(1024, th));
            vn::Image scaled = vn::scaleImage(img, tw, th);
            img.rgba.clear();
            img.rgba.shrink_to_fit();
            tex = std::make_shared<gfx::Texture>();
            if (!tex->create(scaled)) {
                vn::logf(vn::LOG_ERROR, "texture alloc failed for %s (%dx%d)", path.c_str(), tw, th);
                tex.reset();
            }
        } else {
            vn::logf(vn::LOG_WARN, "cannot decode image %s (%s)", path.c_str(), resolved.c_str());
        }
    }
    texCache_.push_front({key, tex});
    while (texCache_.size() > 24) texCache_.pop_back();
    return tex;
}

void Game::ensureLoaded(Scene& s) {
    if (!s.bgLoaded) {
        s.bgLoaded = true;
        s.bgTex = loadTexture(vn::RES_BACKGROUND, s.bg, true);
    }
    for (size_t i = 0; i < s.sprites.size(); i++) {
        if (!s.sprites[i].loaded) {
            s.sprites[i].loaded = true;
            s.sprites[i].tex = loadTexture(vn::RES_FOREGROUND, s.sprites[i].path, false, s.sprites[i].centered);
            if (!s.bgLoaded) return ensureLoaded(s);  // geometry changed while loading
        }
    }
}

bool Game::sameLayer(const Layer& a, const Layer& b) {
    return a.path == b.path && a.x == b.x && a.y == b.y && a.layer == b.layer;
}

void Game::layerPos(const Layer& l, float& x, float& y) const {
    float tw = l.tex ? (float)l.tex->width() : 0, th = l.tex ? (float)l.tex->height() : 0;
    if (l.centered) {
        if (tw >= dispW_ - 1 && th >= dispH_ - 1 && std::fabs(tw - dispW_) < 2) {
            x = dispX_;
            y = dispY_;
        } else {
            // MangaGamer coordinates: 640x480, origin at the centre
            float ky = dispH_ / 480.0f, kx = ky * (spriteKX_ / spriteKY_);
            x = dispX_ + (dispW_ - tw) / 2 + l.x * kx;
            y = dispY_ + (dispH_ - th) / 2 + l.y * ky;
        }
    } else {
        x = dispX_ + l.x * posSX_;
        y = dispY_ + l.y * posSY_;
        if (pinSprites_ && th > 0) y = dispY_ + dispH_ - th;  // stand on the bottom edge (no cut-off legs)
    }
    x = std::floor(x);
    y = std::floor(y);
}

void Game::beginFilm(const Scene& s) {
    if (s.film == 1) C2D_SetTintMode(C2D_TintLuma);
    else if (s.film == 2) C2D_SetTintMode(C2D_TintOneMinusAdd);
}

void Game::endFilm() { C2D_SetTintMode(C2D_TintSolid); }

static void drawFilmTex(const gfx::Texture& t, float x, float y, float alpha, float w, float h, int film, u32 color) {
    if (film == 1) gfx::drawImageTint(t, x, y, alpha, w, h, color, 1.0f);
    else if (film == 2) gfx::drawImageTint(t, x, y, alpha, w, h, 0, 1.0f);
    else gfx::drawImage(t, x, y, alpha, w, h);
}

void Game::drawBg(const Scene& s, float alpha, float dx, float dy, float grow) {
    if (s.bgTex) {
        float gy = grow * dispH_ / std::max(1.0f, dispW_);
        drawFilmTex(*s.bgTex, dispX_ - grow + dx, dispY_ - gy + dy, alpha, dispW_ + 2 * grow, dispH_ + 2 * gy, s.film,
                    s.filmColor);
    } else {
        gfx::drawRect(dispX_, dispY_, dispW_, dispH_, C2D_Color32(0, 0, 0, (u8)(alpha * 255)));
    }
}

void Game::drawLayer(const Scene& s, const Layer& l, float alpha, float dx, float dy) {
    if (!l.tex) return;
    float x, y;
    layerPos(l, x, y);
    drawFilmTex(*l.tex, x + dx, y + dy, alpha, -1, -1, s.film, s.filmColor);
}

void Game::drawScene(Scene& s, float alpha, float bgDx, float spDx, float dy, float grow) {
    beginFilm(s);
    drawBg(s, alpha, bgDx, dy, grow);
    std::vector<int> order(s.sprites.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return s.sprites[a].priority < s.sprites[b].priority; });
    for (int i : order) drawLayer(s, s.sprites[i], alpha, spDx, dy);
    endFilm();
}

void Game::setBackground(const std::string& path, int fadeFrames) {
    std::string p = (path == "~") ? "" : path;
    if (p != shown_.bg) nextBgChanged_ = true;
    next_.bg = p;
    next_.bgLoaded = false;
    next_.bgTex.reset();
    next_.sprites.clear();
    nextFade_ = fadeFrames;
    nextDirty_ = true;
}

void Game::setBackgroundKeep(const std::string& path) {
    std::string p = (path == "~") ? "" : path;
    if (p != shown_.bg) nextBgChanged_ = true;
    next_.bg = p;
    next_.bgLoaded = false;
    next_.bgTex.reset();
    nextDirty_ = true;
}

void Game::addSprite(const std::string& path, int x, int y) {
    Layer l;
    l.path = path;
    l.x = x;
    l.y = y;
    next_.sprites.push_back(l);
    nextDirty_ = true;
}

void Game::setLayer(int layer, const std::string& path, int x, int y, int priority) {
    auto& v = next_.sprites;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Layer& l) { return l.layer == layer; }), v.end());
    Layer l;
    l.path = path;
    l.x = x;
    l.y = y;
    l.layer = layer;
    l.priority = priority;
    l.centered = true;
    v.push_back(l);
    nextDirty_ = true;
}

void Game::clearLayer(int layer) {
    auto& v = next_.sprites;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Layer& l) { return l.layer == layer; }), v.end());
    nextDirty_ = true;
}

void Game::clearLayers() {
    next_.sprites.clear();
    nextDirty_ = true;
}

void Game::setFilm(int mode, int r, int g, int b, int a) {
    int m = (mode == 1 && a < 64) ? 0 : mode;
    u32 c = C2D_Color32((u8)r, (u8)g, (u8)b, 255);
    if (m != next_.film || (m == 1 && c != next_.filmColor)) nextBgChanged_ = true;  // cross-fade the whole picture
    next_.film = m;
    next_.filmColor = c;
    nextDirty_ = true;
}

void Game::shake(int frames, int amplitude) {
    if (skipping_) return;
    shakeFrames_ = frames;
    shakeAmp_ = amplitude;
}

bool Game::flushGraphics(bool instant) { return flushGraphicsFor(instant ? 0 : -1); }

bool Game::flushGraphicsFor(int framesArg) {
    if (!nextDirty_) return false;
    nextDirty_ = false;
    // keep textures already loaded for layers that didn't change
    if (next_.bg == shown_.bg && shown_.bgLoaded) {
        next_.bgTex = shown_.bgTex;
        next_.bgLoaded = true;
    }
    for (auto& l : next_.sprites)
        for (auto& o : shown_.sprites)
            if (!l.loaded && o.loaded && o.path == l.path && o.centered == l.centered) {
                l.tex = o.tex;
                l.loaded = true;
            }
    prev_ = shown_;
    shown_ = next_;
    fadeBg_ = nextBgChanged_;
    nextBgChanged_ = false;
    int frames = framesArg >= 0 ? framesArg : (fadeBg_ ? nextFade_ : 16);
    if (skipping_ || frames <= 0) {
        fadeLeft_ = fadeTotal_ = 0;
        prev_ = Scene();
        return false;
    }
    fadeTotal_ = fadeLeft_ = frames;
    return true;
}

// ---------------------------------------------------------------------------
// Audio

int Game::audioSlot(int kind, int channel) const {
    if (kind == 0) return std::max(0, std::min(2, channel));
    if (kind == 2) return audio::SOUND;
    return 4 + (channel & 3);
}

void Game::playSound(const std::string& path, int repeats) {
    if (path.empty() || path == "~") {
        audio::stop(audio::SOUND);
        return;
    }
    auto s = novel_.openRes(vn::RES_SOUND, path);
    if (!s) return;
    audio::play(audio::SOUND, std::move(s), path, repeats < 0 ? -1 : std::max(1, repeats));
}

void Game::playMusic(const std::string& path) {
    wantMusic_ = (path == "~") ? "" : path;
    musicDirty_ = true;
}

void Game::playAudio(int kind, int channel, const std::string& path, float volume, int loops) {
    int slot = audioSlot(kind, channel);
    if (path.empty() || path == "~") {
        stopAudio(kind, channel, 0);
        return;
    }
    if (kind == 0 && slotName_[slot] == path && audio::isPlaying(slot)) return;  // already playing
    auto s = novel_.openRes(vn::RES_SOUND, path);
    if (!s) return;
    audio::play(slot, std::move(s), path, loops, volume);
    slotName_[slot] = path;
    if (slot == audio::MUSIC) curMusic_ = path;
}

void Game::stopAudio(int kind, int channel, int fadeMs) {
    int slot = audioSlot(kind, channel);
    audio::stop(slot, fadeMs);
    slotName_[slot].clear();
    if (slot == audio::MUSIC) curMusic_.clear();
}

bool Game::audioPlaying(int kind, int channel) { return audio::isPlaying(audioSlot(kind, channel)); }

// ---------------------------------------------------------------------------
// Text

void Game::wrapPara(Para& p) {
    if (p.text.empty()) {
        p.lines = {""};
        p.adv.clear();
        return;
    }
    p.lines = app_.font.wrap(p.text, paneW() - 2 * TEXT_X);
    p.adv = advMode() ? app_.font.wrap(p.text, advTextW()) : std::vector<std::string>();
}

Game::Para& Game::addPara(const std::string& text, u32 color) {
    Para p;
    p.text = text;
    p.color = color;
    p.serial = ++paraSerial_;
    wrapPara(p);
    log_.push_back(std::move(p));
    while (log_.size() > 400) log_.pop_front();
    return log_.back();
}

void Game::appendText(const std::string& text) {
    finishReveal();
    addPara(text, ui::TEXT);
    paraOpen_ = false;
    if (!text.empty() && !skipping_ && g_config.textSpeed < 5) {
        revealActive_ = true;
        revealChars_ = 0;
        revealTotal_ = FontRenderer::countChars(text);
    }
    scrollBack_ = 0;
}

void Game::appendInline(const std::string& text) {
    // Higurashi: text continues the current paragraph until a '\n'.
    size_t shownBefore = 0;
    bool continuing = false;
    auto segs = vn::split(text, '\n');
    for (size_t i = 0; i < segs.size(); i++) {
        if (i > 0) {
            if (!paraOpen_) addPara("", ui::TEXT);  // "\n\n" -> empty line
            paraOpen_ = false;
            finishReveal();
        }
        const std::string& seg = segs[i];
        if (seg.empty()) continue;
        if (paraOpen_ && !log_.empty()) {
            Para& p = log_.back();
            shownBefore = revealActive_ ? (size_t)revealChars_ : FontRenderer::countChars(p.text);
            p.text += seg;
            wrapPara(p);
            continuing = true;
        } else {
            finishReveal();
            addPara(seg, ui::TEXT);
            shownBefore = 0;
            continuing = true;
        }
        paraOpen_ = true;
    }
    if (continuing && !skipping_ && g_config.textSpeed < 5 && !log_.empty()) {
        revealActive_ = true;
        revealChars_ = (float)shownBefore;
        revealTotal_ = FontRenderer::countChars(log_.back().text);
    }
    scrollBack_ = 0;
}

void Game::clearText(bool full) {
    finishReveal();
    paraOpen_ = false;
    clearSerial_ = paraSerial_ + 1;
    if (full) {
        log_.clear();
    } else {
        int n = visibleLines();
        for (int i = 0; i < n; i++) addPara("", ui::TEXT);
    }
    scrollBack_ = 0;
}

void Game::showChoice(const std::vector<std::string>& options) {
    finishReveal();
    if (!skipping_ && playFrames_ > 60 * 20) autoSave();  // at most every 20 s
    choices_ = options;
    choiceSel_ = 0;
    choiceScroll_ = 0;
    skipSticky_ = false;
}

void Game::globalsChanged() {
    vn::writeGlobals(vn::joinPath(novel_.saveDir(), "global.sav"), eng_->globals());
}

void Game::scriptError(const std::string& msg) {
    vn::logf(vn::LOG_ERROR, "%s", msg.c_str());
    addPara("[" + msg + "]", ui::TEXT_ERR).echo = true;
    paraOpen_ = false;
}

bool Game::revealing() const { return revealActive_; }

void Game::finishReveal() { revealActive_ = false; }

void Game::rewrap() {
    for (auto& p : log_) wrapPara(p);
}

float Game::paneH() const { return paneOnTop() ? (float)gfx::TOP_H : (float)(gfx::BOT_H - STATUS_H); }

int Game::visibleLines() const { return std::max(1, (int)((paneH() - TEXT_Y - 2) / app_.font.lineHeight())); }

int Game::advLines() const {
    return std::max(2, std::min(5, (int)(picH_ * 0.33f / app_.font.lineHeight())));
}

// The paragraph shown in the text box: the newest real text since the last clear.
const Game::Para* Game::advPara() const {
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        if (it->serial < clearSerial_) break;
        if (it->text.empty() || it->echo) continue;
        return &*it;
    }
    return nullptr;
}

int Game::totalLines() const {
    int n = 0;
    for (auto& p : log_) n += (int)p.lines.size();
    return n;
}

// ---------------------------------------------------------------------------
// Saves

std::string Game::slotPath(int slot) const {
    char b[32];
    snprintf(b, sizeof(b), "save%02d.sav", slot);
    return vn::joinPath(novel_.saveDir(), b);
}

void Game::toast(const std::string& s) {
    toast_ = s;
    toastFrames_ = 120;
}

int Game::slotOf(int index) {
    if (index < NUM_SAVE_SLOTS) return index;
    return index == NUM_SAVE_SLOTS ? QUICK_SLOT : AUTO_SLOT;
}

void Game::doSave(int slot, bool silent) {
    if (!eng_) return;
    vn::SaveState s = eng_->save();
    if (s.file.empty()) return;
    s.date = vn::nowString();
    s.stamp = (long long)time(nullptr);
    if (s.background.empty()) s.background = shown_.bg;  // for the load-screen preview
    s.pinSprites = pinSprites_ ? 1 : 0;
    bool ok = vn::writeSave(slotPath(slot), s);
    if (slot == AUTO_SLOT) playFrames_ = 0;
    if (silent) return;
    if (ok) toast(slot == QUICK_SLOT ? "Quick saved" : slot == AUTO_SLOT ? "Autosaved" : "Saved to slot " + std::to_string(slot + 1));
    else toast("Save failed: " + slotPath(slot));
}

void Game::autoSave() {
    if (g_config.autoSave && !ended_) doSave(AUTO_SLOT, true);
}

int Game::newestSave(long long* stampOut) {
    int best = -1;
    long long bestStamp = -1;
    for (int i = 0; i < LOAD_ENTRIES; i++) {
        int slot = slotOf(i);
        vn::SaveState s;
        if (!vn::fileExists(slotPath(slot)) || !vn::readSave(slotPath(slot), s)) continue;
        if (s.stamp > bestStamp) {
            bestStamp = s.stamp;
            best = slot;
        }
    }
    if (stampOut) *stampOut = bestStamp;
    return best;
}

void Game::resetForJump() {
    choices_.clear();
    fadeLeft_ = fadeTotal_ = 0;
    prev_ = Scene();
    nextDirty_ = false;
    skipping_ = skipSticky_ = false;
    shakeFrames_ = 0;
    scrollBack_ = 0;
    ended_ = false;
}

bool Game::doLoad(int slot) {
    vn::SaveState s;
    if (!vn::readSave(slotPath(slot), s)) {
        toast("Cannot read save");
        return false;
    }
    resetForJump();
    if (s.pinSprites >= 0 && (s.pinSprites != 0) != pinSprites_) {
        pinSprites_ = s.pinSprites != 0;  // the option travels with the save
        saveNovelPrefs();
    }
    bool ok = eng_->load(s);
    fadeLeft_ = fadeTotal_ = 0;  // keep the restored scene instant
    toast(ok ? "Loaded" : "Load failed");
    return ok;
}

void Game::refreshSlots() {
    slots_.assign(LOAD_ENTRIES, SlotInfo());
    for (int i = 0; i < LOAD_ENTRIES; i++) {
        int slot = slotOf(i);
        SlotInfo& si = slots_[i];
        if (vn::fileExists(slotPath(slot)) && vn::readSave(slotPath(slot), si.state)) {
            si.used = true;
            si.date = si.state.date;
            si.text = si.state.lastText;
        }
    }
    previewFor_ = -1;
}

// ---------------------------------------------------------------------------
// Update

void Game::openOverlay(Overlay o) {
    overlay_ = o;
    if (o == MENU) {
        menu_.title.clear();
        menu_.items.clear();
        menuIds_.clear();
        auto add = [&](int id, const std::string& s) {
            menuIds_.push_back(id);
            menu_.items.push_back(s);
        };
        add(M_RETURN, "Return");
        add(M_SAVE, "Save");
        add(M_LOAD, "Load");
        if (!eng_->chapters().empty()) add(M_CHAPTERS, eng_->tips().empty() ? "Chapters" : "Chapters / TIPS");
        add(M_AUTO, std::string("Auto mode: ") + (autoMode_ ? "ON" : "OFF"));
        add(M_SKIP, "Skip to next choice");
        add(M_SETTINGS, "Settings");
        add(M_RESTART, "Restart from the beginning");
        add(M_EXIT, "Exit to library");
        menu_.enabled.clear();
        menu_.sel = 0;
    } else if (o == SAVE || o == LOAD) {
        refreshSlots();
        if (o == SAVE && slotSel_ >= NUM_SAVE_SLOTS) slotSel_ = 0;
    } else if (o == CONFIRM_EXIT) {
        menu_.title = "Exit to the novel list?";
        menu_.items = {"No", "Yes"};
        menu_.enabled.clear();
        menu_.sel = 0;
    } else if (o == CHAPTERS) {
        listItems_ = eng_->chapters();
        listChapters_ = (int)listItems_.size();
        auto t = eng_->tips();
        if (!t.empty()) {
            listItems_.push_back("— TIPS —");
            for (auto& x : t) listItems_.push_back(x);
        }
        listSel_ = std::min(listSel_, (int)listItems_.size() - 1);
        if (listSel_ == listChapters_) listSel_ = 0;
    }
}

void Game::runScript(bool fast) {
    if (!eng_) return;
    if (fast) {
        // Skip shows every line flying by (one per frame, ~60 lines/s) instead of
        // jumping over whole blocks; holding R for a while speeds it up.
        static int heldFrames = 0;
        heldFrames = skipping_ ? heldFrames + 1 : 0;
        int linesPerFrame = heldFrames > 240 ? 4 : heldFrames > 120 ? 2 : 1;
        int lines = 0;
        for (int i = 0; i < 200; i++) {
            auto st = eng_->run(true);
            if (st == Engine::WAIT_INPUT) {
                finishReveal();
                if (lines >= linesPerFrame - 1) break;  // show this line for a frame (the caller advanced one already)
                eng_->advance();
                lines++;
            } else if (st == Engine::WAIT_DELAY) {
                eng_->cancelDelay();
            } else if (st == Engine::WAIT_FX) {
                eng_->resume();
            } else if (st != Engine::RUNNING) {
                break;
            }
        }
    } else if (eng_->state() == Engine::RUNNING) {
        eng_->run(false);
    }
    if (eng_->state() == Engine::ENDED && !ended_) {
        ended_ = true;
        finishReveal();
        paraOpen_ = false;
        addPara("", ui::TEXT);
        addPara("— END —", ui::ACCENT).echo = false;
    }
}

void Game::updateGame(const ui::Input& in) {
    // menus
    if (in.down & (KEY_X | KEY_START)) {
        openOverlay(MENU);
        return;
    }
    if (in.down & KEY_SELECT) doSave(QUICK_SLOT);
    if (in.down & KEY_Y) {
        autoMode_ = !autoMode_;
        autoWait_ = 0;
        toast(autoMode_ ? "Auto mode ON" : "Auto mode OFF");
    }
    if (ended_) {
        if ((in.down & (KEY_A | KEY_B)) || in.tapped) exit_ = true;
        return;
    }
    if (advMode()) {  // B hides the text box to look at the picture, like most VN engines
        if (in.down & KEY_B) {
            hideBox_ = !hideBox_;
            return;
        }
        if (hideBox_ && ((in.down & KEY_A) || in.tapped)) {
            hideBox_ = false;
            return;
        }
    }

    // log scrolling
    int maxBack = std::max(0, totalLines() - visibleLines());
    static int repeat = 0;
    bool upHeld = in.held & (KEY_UP | KEY_CPAD_UP), downHeld = in.held & (KEY_DOWN | KEY_CPAD_DOWN);
    if (upHeld || downHeld) {
        if ((in.down & (KEY_UP | KEY_CPAD_UP | KEY_DOWN | KEY_CPAD_DOWN)) || (++repeat > 15 && repeat % 3 == 0))
            scrollBack_ += upHeld ? 1 : -1;
    } else {
        repeat = 0;
    }
    if (in.touchHeld && in.dragDY && choices_.empty()) {
        static float acc = 0;
        acc += in.dragDY;
        int lines = (int)(acc / app_.font.lineHeight());
        if (lines) {
            scrollBack_ += lines;
            acc -= lines * app_.font.lineHeight();
        }
    }
    scrollBack_ = std::max(0, std::min(scrollBack_, maxBack));

    skipping_ = (in.held & KEY_R) || skipSticky_;
    bool advance = (in.down & KEY_A) || (in.tapped && choices_.empty());
    if (advance && scrollBack_ > 0) {
        scrollBack_ = 0;
        advance = false;
    }

    // transitions
    if (fadeLeft_ > 0) {
        fadeLeft_ = skipping_ || advance ? 0 : fadeLeft_ - 1;
        advance = false;
        if (fadeLeft_ == 0) prev_ = Scene();
    }
    if (eng_->state() == Engine::WAIT_FX && fadeLeft_ == 0) eng_->resume();

    switch (eng_->state()) {
        case Engine::WAIT_CHOICE: {
            int n = (int)choices_.size();
            if (n == 0) break;
            if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) choiceSel_ = (choiceSel_ + 1) % n;
            if (in.down & (KEY_UP | KEY_CPAD_UP)) choiceSel_ = (choiceSel_ - 1 + n) % n;
            int pick = -1;
            if (in.down & KEY_A) pick = choiceSel_;
            const float rowH = 34, w = gfx::BOT_W - 24;
            float total = n * rowH;
            float avail = gfx::BOT_H - STATUS_H - 8;
            float y0 = total < avail ? (avail - total) / 2 + 4 : 4 - choiceScroll_;
            if (in.touchHeld && in.dragDY && total > avail) choiceScroll_ -= in.dragDY;
            choiceScroll_ = std::max(0.0f, std::min(choiceScroll_, std::max(0.0f, total - avail)));
            for (int i = 0; i < n; i++)
                if (ui::hit(in, 12, y0 + i * rowH, w, rowH - 4)) pick = i;
            if (pick >= 0) {
                addPara("> " + choices_[pick], ui::ACCENT).echo = true;
                paraOpen_ = false;
                choices_.clear();
                eng_->choose(pick);
            }
            break;
        }
        case Engine::WAIT_INPUT:
            if (revealing()) {
                if (advance || skipping_) finishReveal();
            } else if (skipping_ || advance) {
                eng_->advance();
                autoWait_ = 0;
            } else if (autoMode_) {
                if (!audio::isPlaying(audio::SOUND)) autoWait_++;
                int need = 30 + g_config.autoDelay * 20 + (log_.empty() ? 0 : (int)log_.back().text.size() / 2);
                if (autoWait_ > need) {
                    autoWait_ = 0;
                    eng_->advance();
                }
            }
            break;
        case Engine::WAIT_DELAY:
            if (skipping_ || (in.down & (KEY_A | KEY_B)) || in.tapped) eng_->cancelDelay();
            else eng_->tickDelay(1);
            break;
        default:
            break;
    }
    runScript(skipping_ && eng_->state() != Engine::WAIT_CHOICE);
    if (++playFrames_ > AUTOSAVE_FRAMES && eng_->state() == Engine::WAIT_INPUT && !skipping_) autoSave();

    // typewriter (waits for the picture transition, like the DS)
    if (revealActive_ && fadeLeft_ == 0) {
        revealChars_ += REVEAL_SPEED[g_config.textSpeed];
        if (revealChars_ >= revealTotal_) revealActive_ = false;
    }
}

void Game::updateOverlay(const ui::Input& in) {
    switch (overlay_) {
        case MENU: {
            if (in.down & (KEY_X | KEY_START)) {
                overlay_ = NONE;
                break;
            }
            float rowH = std::min(32.0f, 228.0f / menu_.items.size());
            int r = menu_.update(in, 20, 6, gfx::BOT_W - 40, rowH);
            if (r == -2) {
                overlay_ = NONE;
                break;
            }
            if (r < 0) break;
            switch (menuIds_[r]) {
                case M_RETURN: overlay_ = NONE; break;
                case M_SAVE: openOverlay(SAVE); break;
                case M_LOAD: openOverlay(LOAD); break;
                case M_CHAPTERS: openOverlay(CHAPTERS); break;
                case M_AUTO:
                    autoMode_ = !autoMode_;
                    overlay_ = NONE;
                    break;
                case M_SKIP:
                    skipSticky_ = true;
                    overlay_ = NONE;
                    break;
                case M_SETTINGS:
                    settingSel_ = 0;
                    overlay_ = SETTINGS;
                    break;
                case M_RESTART:
                    resetForJump();
                    audio::stopAll();
                    for (auto& n : slotName_) n.clear();
                    curMusic_.clear();
                    clearText(true);
                    clearLayers();
                    setFilm(0, 0, 0, 0, 0);
                    setBackground("", 0);
                    flushGraphicsFor(0);
                    eng_->start();
                    overlay_ = NONE;
                    toast("Started from the beginning");
                    break;
                case M_EXIT: openOverlay(CONFIRM_EXIT); break;
            }
            break;
        }
        case CONFIRM_EXIT: {
            int r = menu_.update(in, 40, 60, gfx::BOT_W - 80, 40);
            if (r == 1) exit_ = true;
            else if (r == 0 || r == -2) openOverlay(MENU);
            break;
        }
        case CHAPTERS: {
            int n = (int)listItems_.size();
            const float rowH = 30;
            const int vis = 7;
            if (in.down & KEY_B) {
                openOverlay(MENU);
                break;
            }
            auto step = [&](int d) {
                for (int k = 0; k < n; k++) {
                    listSel_ = (listSel_ + d + n) % n;
                    if (listSel_ != listChapters_) break;
                }
            };
            if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) step(1);
            if (in.down & (KEY_UP | KEY_CPAD_UP)) step(-1);
            if (in.down & KEY_RIGHT) listSel_ = std::min(n - 1, listSel_ + vis);
            if (in.down & KEY_LEFT) listSel_ = std::max(0, listSel_ - vis);
            if (listSel_ == listChapters_) step(1);
            if (listSel_ < listScroll_) listScroll_ = listSel_;
            if (listSel_ >= listScroll_ + vis) listScroll_ = listSel_ - vis + 1;
            int pick = -1;
            if (in.down & KEY_A) pick = listSel_;
            for (int i = 0; i < vis && listScroll_ + i < n; i++)
                if (ui::hit(in, 4, 4 + i * rowH, gfx::BOT_W - 8, rowH - 3) && listScroll_ + i != listChapters_) {
                    if (listSel_ == listScroll_ + i) pick = listSel_;
                    listSel_ = listScroll_ + i;
                }
            if (pick >= 0 && pick != listChapters_) {
                resetForJump();
                bool ok = pick < listChapters_ ? eng_->jumpToChapter(pick) : eng_->playTip(pick - listChapters_ - 1);
                if (ok) overlay_ = NONE;
                else toast("Cannot open that script");
            }
            break;
        }
        case SAVE:
        case LOAD: {
            int n = overlay_ == SAVE ? NUM_SAVE_SLOTS : LOAD_ENTRIES;
            const float rowH = 36;
            const int vis = 6;
            if (in.down & KEY_B) {
                openOverlay(MENU);
                break;
            }
            if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) slotSel_ = (slotSel_ + 1) % n;
            if (in.down & (KEY_UP | KEY_CPAD_UP)) slotSel_ = (slotSel_ - 1 + n) % n;
            if (in.down & KEY_RIGHT) slotSel_ = std::min(n - 1, slotSel_ + vis);
            if (in.down & KEY_LEFT) slotSel_ = std::max(0, slotSel_ - vis);
            if (slotSel_ >= n) slotSel_ = 0;
            if (slotSel_ < slotScroll_) slotScroll_ = slotSel_;
            if (slotSel_ >= slotScroll_ + vis) slotScroll_ = slotSel_ - vis + 1;
            int pick = -1;
            if (in.down & KEY_A) pick = slotSel_;
            for (int i = 0; i < vis && slotScroll_ + i < n; i++)
                if (ui::hit(in, 4, 4 + i * rowH, gfx::BOT_W - 8, rowH - 3)) {
                    if (slotSel_ == slotScroll_ + i) pick = slotSel_;
                    slotSel_ = slotScroll_ + i;
                }
            if (pick >= 0) {
                int slot = slotOf(pick);
                if (overlay_ == SAVE) {
                    doSave(slot);
                    overlay_ = NONE;
                } else if (slots_[pick].used) {
                    if (doLoad(slot)) overlay_ = NONE;
                }
            }
            break;
        }
        case SETTINGS: {
            const int N = 11, VIS = 7;
            if (in.down & (KEY_B | KEY_X | KEY_START)) {
                g_config.save();
                bool layoutChanged = g_config.screenLayout != layout_;
                layout_ = g_config.screenLayout;
                if (g_config.fitMode != fitUsed_ || layoutChanged) computeGeometry();
                if (g_config.fontSize != fontSizeUsed_ || layoutChanged) {
                    app_.font.setSize(g_config.fontSize);
                    fontSizeUsed_ = g_config.fontSize;
                    rewrap();
                }
                hideBox_ = false;
                overlay_ = NONE;
                break;
            }
            if (in.down & (KEY_DOWN | KEY_CPAD_DOWN)) settingSel_ = (settingSel_ + 1) % N;
            if (in.down & (KEY_UP | KEY_CPAD_UP)) settingSel_ = (settingSel_ - 1 + N) % N;
            if (in.touchHeld && in.dragDY) {
                static float acc = 0;
                acc -= in.dragDY;
                int rows = (int)(acc / 30);
                if (rows) {
                    settingScroll_ += rows;
                    acc -= rows * 30;
                }
            } else {
                if (settingSel_ < settingScroll_) settingScroll_ = settingSel_;
                if (settingSel_ >= settingScroll_ + VIS) settingScroll_ = settingSel_ - VIS + 1;
            }
            settingScroll_ = std::max(0, std::min(settingScroll_, N - VIS));
            int delta = 0;
            if (in.down & (KEY_RIGHT | KEY_CPAD_RIGHT | KEY_A)) delta = 1;
            if (in.down & (KEY_LEFT | KEY_CPAD_LEFT)) delta = -1;
            for (int r = 0; r < VIS; r++) {
                int i = settingScroll_ + r;
                float y = 24 + r * 30;
                if (ui::hit(in, 8, y, 60, 28)) {
                    settingSel_ = i;
                    delta = -1;
                } else if (ui::hit(in, gfx::BOT_W - 68, y, 60, 28)) {
                    settingSel_ = i;
                    delta = 1;
                } else if (ui::hit(in, 68, y, gfx::BOT_W - 136, 28)) {
                    settingSel_ = i;
                }
            }
            if (delta) {
                auto clampv = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
                switch (settingSel_) {
                    case 0: g_config.textSpeed = clampv(g_config.textSpeed + delta, 0, 5); break;
                    case 1: g_config.musicVolume = clampv(g_config.musicVolume + delta * 10, 0, 100); break;
                    case 2: g_config.soundVolume = clampv(g_config.soundVolume + delta * 10, 0, 100); break;
                    case 3: g_config.fontSize = clampv(g_config.fontSize + delta, 11, 22); break;
                    case 4: g_config.autoDelay = clampv(g_config.autoDelay + delta, 1, 9); break;
                    case 5: g_config.depth3D = clampv(g_config.depth3D + delta, 0, 3); break;
                    case 6: g_config.fitMode = (g_config.fitMode + delta + 3) % 3; break;
                    case 7: g_config.screenLayout = (g_config.screenLayout + delta + 4) % 4; break;
                    case 8:
                        pinSprites_ = !pinSprites_;
                        saveNovelPrefs();
                        break;
                    case 9: g_config.autoSave = !g_config.autoSave; break;
                    case 10: g_config.resumeLast = !g_config.resumeLast; break;
                }
                applyAudioSettings();
            }
            break;
        }
        default:
            break;
    }
}

void Game::update(const ui::Input& in) {
    frame_++;
    if (toastFrames_ > 0) toastFrames_--;
    if (shakeFrames_ > 0) {
        shakeFrames_--;
        shakeX_ = (float)(rand() % (2 * shakeAmp_ + 1) - shakeAmp_);
        shakeY_ = (float)(rand() % (2 * shakeAmp_ + 1) - shakeAmp_) * 0.5f;
    } else {
        shakeX_ = shakeY_ = 0;
    }
    if (overlay_ != NONE) {
        updateOverlay(in);
        // fades keep running behind menus
        if (fadeLeft_ > 0 && --fadeLeft_ == 0) prev_ = Scene();
    } else {
        updateGame(in);
    }
    if (musicDirty_) {
        musicDirty_ = false;
        if (wantMusic_.empty()) {
            audio::stop(audio::MUSIC);
            curMusic_.clear();
            slotName_[audio::MUSIC].clear();
        } else if (wantMusic_ != curMusic_ || !audio::isPlaying(audio::MUSIC)) {
            curMusic_ = wantMusic_;
            slotName_[audio::MUSIC] = curMusic_;
            if (auto s = novel_.openRes(vn::RES_SOUND, curMusic_)) audio::play(audio::MUSIC, std::move(s), curMusic_, -1);
            else audio::stop(audio::MUSIC);
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing

void Game::drawPreview(float eye, float ox) {
    // picture of the selected save slot (on the top screen, whatever the layout)
    if (previewFor_ != slotSel_) {
        previewFor_ = slotSel_;
        preview_ = Scene();
        const SlotInfo& si = slots_[slotSel_];
        if (si.used) {
            auto strip = [](std::string p, const char* pre) {
                return vn::startsWith(vn::toLower(p), pre) ? p.substr(strlen(pre)) : p;
            };
            preview_.bg = strip(si.state.background, "background/");
            for (auto& sp : si.state.sprites) {
                Layer l;
                l.path = strip(sp.path, "foreground/");
                l.x = sp.x;
                l.y = sp.y;
                preview_.sprites.push_back(l);
            }
        }
    }
    ensureLoaded(preview_);
    drawScene(preview_, 1.0f, eye + ox, -eye * 0.35f + ox, 0, std::fabs(eye));
    const SlotInfo& si = slots_[slotSel_];
    gfx::drawRect(0, gfx::TOP_H - 40, gfx::TOP_W, 40, ui::SHADOW);
    app_.font.setSize(13);
    std::string title = slotSel_ < NUM_SAVE_SLOTS ? "Slot " + std::to_string(slotSel_ + 1)
                        : slotSel_ == NUM_SAVE_SLOTS ? "Quick save" : "Autosave";
    app_.font.draw(title + (si.used ? "   " + si.date : "   (empty)"), 8, gfx::TOP_H - 38, ui::ACCENT);
    if (si.used) app_.font.draw(ui::ellipsize(app_, si.text, gfx::TOP_W - 16), 8, gfx::TOP_H - 20, ui::TEXT);
    app_.font.setSize(fontSizeUsed_);
}

void Game::drawPicture(float eye) {
    gfx::drawRect(0, 0, picW_, picH_, C2D_Color32(0, 0, 0, 255));
    // Stereoscopic 3D: background behind the screen, sprites slightly in front.
    float bgDx = eye + shakeX_, spDx = -eye * 0.35f + shakeX_, dy = shakeY_;
    float grow = std::fabs(eye) + std::fabs(shakeX_);

    ensureLoaded(shown_);
    if (fadeLeft_ > 0 && fadeTotal_ > 0) {
        float t = 1.0f - (float)fadeLeft_ / fadeTotal_;
        if (fadeBg_) {
            // cross-fade: new picture underneath, old one fading out on top, new sprites in
            beginFilm(shown_);
            drawBg(shown_, 1.0f, bgDx, dy, grow);
            endFilm();
            drawScene(prev_, 1.0f - t, bgDx, spDx, dy, grow);
            beginFilm(shown_);
            for (auto& l : shown_.sprites) drawLayer(shown_, l, t, spDx, dy);
            endFilm();
        } else {
            // same background: unchanged sprites stay, removed ones fade out, new ones fade in
            beginFilm(shown_);
            drawBg(shown_, 1.0f, bgDx, dy, grow);
            for (auto& l : prev_.sprites) {
                bool kept = false;
                for (auto& o : shown_.sprites)
                    if (sameLayer(o, l)) kept = true;
                if (!kept) drawLayer(prev_, l, 1.0f - t, spDx, dy);
            }
            for (auto& l : shown_.sprites) {
                bool kept = false;
                for (auto& o : prev_.sprites)
                    if (sameLayer(o, l)) kept = true;
                drawLayer(shown_, l, kept ? 1.0f : t, spDx, dy);
            }
            endFilm();
        }
    } else {
        drawScene(shown_, 1.0f, bgDx, spDx, dy, grow);
    }
    // letterbox: hide sprite parts outside the picture
    const u32 black = C2D_Color32(0, 0, 0, 255);
    if (dispX_ > 0) {  // (zoom mode has negative offsets: the screen edge crops instead)
        gfx::drawRect(0, 0, dispX_, picH_, black);
        gfx::drawRect(dispX_ + dispW_, 0, picW_ - dispX_ - dispW_, picH_, black);
    }
    if (dispY_ > 0) {
        gfx::drawRect(0, 0, picW_, dispY_, black);
        gfx::drawRect(0, dispY_ + dispH_, picW_, picH_ - dispY_ - dispH_, black);
    }
}

void Game::drawToast(float w, float y) {
    if (toastFrames_ <= 0) return;
    app_.font.setSize(13);
    float tw = app_.font.measure(toast_) + 16;
    gfx::drawRect((w - tw) / 2, y, tw, 20, ui::SHADOW);
    app_.font.draw(toast_, (w - tw) / 2 + 8, y + 2, ui::TEXT);
    app_.font.setSize(fontSizeUsed_);
}

void Game::drawTextPane(float w, float h, bool dimAll) {
    app_.font.setSize(fontSizeUsed_);
    float lh = app_.font.lineHeight();
    int vis = visibleLines();
    int total = totalLines();
    int end = total - scrollBack_;
    int start = std::max(0, end - vis);
    int idx = 0;
    float y = TEXT_Y;
    for (size_t pi = 0; pi < log_.size(); pi++) {
        const Para& p = log_[pi];
        bool last = pi + 1 == log_.size();
        if (idx + (int)p.lines.size() <= start) {
            idx += (int)p.lines.size();
            continue;
        }
        size_t charsLeft = (last && revealActive_) ? (size_t)revealChars_ : (size_t)-1;
        u32 col = ((last && !dimAll) || p.color != ui::TEXT) ? p.color : TEXT_OLD;
        for (auto& line : p.lines) {
            size_t lc = FontRenderer::countChars(line);
            if (idx >= start && idx < end) {
                app_.font.draw(line, TEXT_X, y, col, charsLeft);
                y += lh;
            }
            charsLeft = charsLeft == (size_t)-1 ? charsLeft : (charsLeft > lc ? charsLeft - lc : 0);
            idx++;
        }
    }
    // "click to continue" marker
    if (!dimAll && eng_ && eng_->state() == Engine::WAIT_INPUT && !revealActive_ && scrollBack_ == 0 &&
        (frame_ / 20) % 2 == 0 && overlay_ == NONE) {
        float yy = std::min(y, h - lh) + lh * 0.25f;
        C2D_DrawTriangle(w - 18, yy, ui::ACCENT, w - 8, yy, ui::ACCENT, w - 13, yy + 7, ui::ACCENT, 0.5f);
    }
    if (scrollBack_ > 0) {
        float frac = total > vis ? (float)(end - vis) / (total - vis) : 1.0f;
        float bh = std::max(12.0f, h * vis / (float)std::max(1, total));
        gfx::drawRect(w - 3, frac * (h - bh), 3, bh, ui::ACCENT);
    }
}

void Game::drawAdvBox(float dx) {
    app_.font.setSize(fontSizeUsed_);
    float lh = app_.font.lineHeight();
    int lines = advLines();
    float bx = 6 + dx, bw = picW_ - 12, bh = lines * lh + 12, by = picH_ - bh - 6;
    gfx::drawRect(bx, by, bw, bh, C2D_Color32(8, 8, 18, 190));
    gfx::drawRect(bx, by, bw, 1, C2D_Color32(255, 255, 255, 60));
    const Para* p = advPara();
    if (!p) return;
    bool last = p == &log_.back();
    const size_t ALL = (size_t)-1;
    size_t charsLeft = (last && revealActive_) ? (size_t)revealChars_ : ALL;
    int n = (int)p->adv.size(), first = 0;
    if (n > lines) {
        // long paragraph: follow the typewriter, then show the end
        int cur = n - 1;
        if (charsLeft != ALL) {
            size_t c = charsLeft;
            for (cur = 0; cur < n - 1; cur++) {
                size_t lc = FontRenderer::countChars(p->adv[cur]);
                if (c <= lc) break;
                c -= lc;
            }
        }
        first = std::max(0, std::min(cur - lines + 1, n - lines));
    }
    float y = by + 6;
    for (int i = 0; i < n && i < first + lines; i++) {
        size_t lc = FontRenderer::countChars(p->adv[i]);
        if (i >= first) {
            app_.font.draw(p->adv[i], bx + 8, y, p->color, charsLeft);
            y += lh;
        }
        if (charsLeft != ALL) charsLeft = charsLeft > lc ? charsLeft - lc : 0;
    }
    if (eng_ && eng_->state() == Engine::WAIT_INPUT && !revealActive_ && (frame_ / 20) % 2 == 0 &&
        overlay_ == NONE) {
        float tx = bx + bw - 16, ty = by + bh - 12;
        C2D_DrawTriangle(tx, ty, ui::ACCENT, tx + 10, ty, ui::ACCENT, tx + 5, ty + 7, ui::ACCENT, 0.5f);
    }
}

void Game::drawChoices(float w, float avail, bool touch) {
    gfx::drawRect(0, 0, w, gfx::TOP_H, C2D_Color32(0, 0, 0, 150));
    int n = (int)choices_.size();
    const float rowH = 34, bw = w - 24;
    float total = n * rowH;
    float scroll = touch ? choiceScroll_ : 0;
    if (total >= avail) {
        float want = choiceSel_ * rowH;
        if (!touch) scroll = std::max(0.0f, want + rowH - avail);
        if (want < scroll) scroll = want;
        if (want + rowH > scroll + avail) scroll = want + rowH - avail;
        if (touch) choiceScroll_ = scroll;
    }
    float y0 = total < avail ? (avail - total) / 2 + 4 : 4 - scroll;
    for (int i = 0; i < n; i++) {
        float y = y0 + i * rowH;
        if (y + rowH < 0 || y > avail + 4) continue;
        ui::button(app_, 12, y, bw, rowH - 4, choices_[i], i == choiceSel_);
    }
}

static void drawTag(ui::App& app, const std::string& s, float w) {
    if (s.empty()) return;
    app.font.setSize(11);
    float tw = app.font.measure(s) + 10;
    gfx::drawRect(w - tw - 4, 4, tw, 15, ui::SHADOW);
    app.font.draw(s, w - tw + 1, 5, ui::ACCENT);
}

void Game::drawTop(float eye) {
    gfx::drawRect(0, 0, gfx::TOP_W, gfx::TOP_H, C2D_Color32(0, 0, 0, 255));
    if ((overlay_ == SAVE || overlay_ == LOAD) && slotSel_ < (int)slots_.size()) {
        drawPreview(eye, picOnTop() ? 0.0f : std::floor((gfx::TOP_W - picW_) / 2));
        return;
    }
    if (picOnTop()) {
        drawPicture(eye);
        if (layout_ == 2) {
            if (!hideBox_) drawAdvBox(-eye * 0.5f);  // the box floats slightly in front in 3D
            if (!choices_.empty() && overlay_ == NONE) drawChoices(gfx::TOP_W, gfx::TOP_H - 8, false);
            std::string st = skipping_ ? "SKIP" : autoMode_ ? "AUTO" : "";
            drawTag(app_, st, gfx::TOP_W);
            app_.font.setSize(fontSizeUsed_);
        }
        drawToast(gfx::TOP_W, layout_ == 2 ? 24 : gfx::TOP_H - 26);
    } else {
        gfx::drawRect(0, 0, gfx::TOP_W, gfx::TOP_H, ui::BG);
        drawTextPane(gfx::TOP_W, gfx::TOP_H, layout_ == 3);
        drawToast(gfx::TOP_W, gfx::TOP_H - 26);
    }
}

void Game::drawBottom() {
    gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, ui::BG);
    app_.font.setSize(fontSizeUsed_);
    if (picOnTop()) {
        drawTextPane(gfx::BOT_W, gfx::BOT_H - STATUS_H, layout_ == 2);
    } else {
        drawPicture(0);
        if (layout_ == 3 && !hideBox_) drawAdvBox(0);
    }
    if (!choices_.empty() && overlay_ == NONE) drawChoices(gfx::BOT_W, gfx::BOT_H - STATUS_H - 8, true);

    // overlays
    switch (overlay_) {
        case MENU:
        case CONFIRM_EXIT:
            gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, C2D_Color32(10, 10, 16, 225));
            if (overlay_ == MENU) menu_.draw(app_, 20, 6, gfx::BOT_W - 40, std::min(32.0f, 228.0f / menu_.items.size()));
            else menu_.draw(app_, 40, 60, gfx::BOT_W - 80, 40);
            break;
        case CHAPTERS: {
            gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, ui::BG);
            const float rowH = 30;
            for (int i = 0; i < 7 && listScroll_ + i < (int)listItems_.size(); i++) {
                int k = listScroll_ + i;
                float y = 4 + i * rowH;
                app_.font.setSize(13);
                if (k == listChapters_) {
                    ui::textCentered(app_, listItems_[k], gfx::BOT_W / 2, y + 6, ui::ACCENT, 13);
                    continue;
                }
                gfx::drawRect(4, y, gfx::BOT_W - 8, rowH - 3, k == listSel_ ? ui::PANEL_HI : ui::PANEL);
                app_.font.draw(ui::ellipsize(app_, listItems_[k], gfx::BOT_W - 20), 10, y + 6, ui::TEXT);
            }
            app_.font.setSize(fontSizeUsed_);
            ui::hint(app_, "A: play from here   B: back");
            return;
        }
        case SAVE:
        case LOAD: {
            gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, ui::BG);
            int n = overlay_ == SAVE ? NUM_SAVE_SLOTS : LOAD_ENTRIES;
            const float rowH = 36;
            for (int i = 0; i < 6 && slotScroll_ + i < n; i++) {
                int s = slotScroll_ + i;
                float y = 4 + i * rowH;
                gfx::drawRect(4, y, gfx::BOT_W - 8, rowH - 3, s == slotSel_ ? ui::PANEL_HI : ui::PANEL);
                const SlotInfo& si = slots_[s];
                std::string name = s < NUM_SAVE_SLOTS ? std::to_string(s + 1) : s == NUM_SAVE_SLOTS ? "Q" : "A";
                app_.font.setSize(13);
                app_.font.draw(name, 10, y + 2, ui::ACCENT);
                app_.font.draw(si.used ? si.date : "— empty —", 40, y + 2, si.used ? ui::TEXT : ui::TEXT_DIM);
                if (si.used) {
                    app_.font.setSize(11);
                    app_.font.draw(ui::ellipsize(app_, si.text, gfx::BOT_W - 52), 40, y + 18, ui::TEXT_DIM);
                }
            }
            app_.font.setSize(fontSizeUsed_);
            ui::hint(app_, overlay_ == SAVE ? "SAVE  -  A: save here   B: back" : "LOAD  -  A: load   B: back");
            return;
        }
        case SETTINGS: {
            gfx::drawRect(0, 0, gfx::BOT_W, gfx::BOT_H, ui::BG);
            static const char* speedNames[] = {"Very slow", "Slow", "Normal", "Fast", "Very fast", "Instant"};
            static const char* depthNames[] = {"Off", "Low", "Medium", "High"};
            static const char* fitNames[] = {"Fit (black bars)", "Zoom (fill, crop edges)", "Stretch (fill)"};
            static const char* layoutNames[] = {"Picture top, text bottom", "Text top, picture bottom",
                                                "One screen: top (text over picture)",
                                                "One screen: bottom (text over picture)"};
            const int N = 11;
            std::string vals[N] = {speedNames[g_config.textSpeed], std::to_string(g_config.musicVolume) + "%",
                                   std::to_string(g_config.soundVolume) + "%", std::to_string(g_config.fontSize) + " px",
                                   std::to_string(g_config.autoDelay), depthNames[g_config.depth3D],
                                   fitNames[g_config.fitMode], layoutNames[g_config.screenLayout],
                                   pinSprites_ ? "On (saved with this novel's saves)" : "Off",
                                   g_config.autoSave ? "On" : "Off", g_config.resumeLast ? "On" : "Off"};
            const char* names[N] = {"Text speed", "Music volume", "Sound volume", "Font size", "Auto mode delay",
                                    "3D depth (use the 3D slider)", "Screen fit", "Screen layout",
                                    "Sprites stand on the bottom edge", "Autosave (every 2 min, on exit)",
                                    "Continue from the last save on start"};
            ui::textCentered(app_, "Settings", gfx::BOT_W / 2, 3, ui::ACCENT, 14);
            for (int r = 0; r < 7; r++) {
                int i = settingScroll_ + r;
                if (i >= N) break;
                float y = 24 + r * 30;
                gfx::drawRect(4, y, gfx::BOT_W - 8, 28, i == settingSel_ ? ui::PANEL_HI : ui::PANEL);
                app_.font.setSize(12);
                app_.font.draw("◀", 16, y + 7, ui::TEXT_DIM);
                app_.font.draw("▶", gfx::BOT_W - 28, y + 7, ui::TEXT_DIM);
                app_.font.draw(names[i], 44, y - 1, ui::TEXT_DIM);
                app_.font.setSize(13);
                app_.font.draw(vals[i], 44, y + 12, ui::TEXT);
            }
            if (settingScroll_ > 0) ui::textCentered(app_, "▲", gfx::BOT_W - 12, 3, ui::TEXT_DIM, 11);
            if (settingScroll_ + 7 < N) ui::textCentered(app_, "▼", gfx::BOT_W - 12, 226, ui::TEXT_DIM, 11);
            app_.font.setSize(fontSizeUsed_);
            ui::hint(app_, "Left/Right: change   B: done");
            return;
        }
        default:
            break;
    }

    // status line
    std::string st;
    if (skipping_) st = "SKIP  ";
    else if (autoMode_) st = "AUTO  ";
    if (scrollBack_ > 0) st += "LOG  ";
    if (!picOnTop()) {
        drawTag(app_, vn::trim(st), gfx::BOT_W);  // the picture fills this screen
        app_.font.setSize(fontSizeUsed_);
        return;
    }
    ui::hint(app_, st + (ended_ ? "A: back to list"
                         : layout_ == 2 ? "A: next  B: hide text  R: skip  Y: auto  X: menu"
                                        : "A: next  R: skip  Y: auto  X: menu  SELECT: quicksave"));
}
