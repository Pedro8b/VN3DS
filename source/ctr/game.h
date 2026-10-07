// In-game screen: renders the scene on the top screen (optionally in stereoscopic 3D),
// text/choices/menus on the bottom screen, and implements vn::ScriptHost for the
// interpreters (VNDS and Higurashi).
#pragma once
#include <deque>
#include <list>
#include <memory>

#include "../core/script.h"
#include "../core/vfs.h"
#include "ui.h"

class Game : public vn::ScriptHost {
public:
    explicit Game(ui::App& app) : app_(app) {}
    ~Game() override;
    bool open(const vn::NovelEntry& e, std::string& err);
    void update(const ui::Input& in);
    // eyeShift: horizontal parallax in pixels for this eye (0 = no 3D)
    void drawTop(float eyeShift = 0.0f);
    void drawBottom();
    bool finished() const { return exit_; }

    // ScriptHost
    void setBackground(const std::string& path, int fadeFrames) override;
    void addSprite(const std::string& path, int x, int y) override;
    bool flushGraphics(bool instant) override;
    void playSound(const std::string& path, int repeats) override;
    void playMusic(const std::string& path) override;
    void appendText(const std::string& text) override;
    void clearText(bool full) override;
    void showChoice(const std::vector<std::string>& options) override;
    void globalsChanged() override;
    void scriptError(const std::string& msg) override;
    bool flushGraphicsFor(int frames) override;
    void appendInline(const std::string& text) override;
    void setLayer(int layer, const std::string& path, int x, int y, int priority) override;
    void clearLayer(int layer) override;
    void clearLayers() override;
    void setBackgroundKeep(const std::string& path) override;
    void setFilm(int mode, int r, int g, int b, int a) override;
    void shake(int frames, int amplitude) override;
    void playAudio(int kind, int channel, const std::string& path, float volume, int loops) override;
    void stopAudio(int kind, int channel, int fadeMs) override;
    bool audioPlaying(int kind, int channel) override;

private:
    struct Layer {
        std::string path;
        int x = 0, y = 0;
        int layer = -1, priority = 0;
        bool centered = false;  // Higurashi bustshot coordinates
        gfx::TexPtr tex;
        bool loaded = false;
    };
    struct Scene {
        std::string bg;
        gfx::TexPtr bgTex;
        bool bgLoaded = false;
        std::vector<Layer> sprites;
        int film = 0;  // 0 none, 1 monochrome tint, 2 negative
        u32 filmColor = 0xFFFFFFFF;
    };
    struct Para {
        std::string text;
        std::vector<std::string> lines;  // wrapped for the text pane
        std::vector<std::string> adv;    // wrapped for the text box over the picture
        u32 color;
        int serial = 0;
        bool echo = false;  // chosen option / system line (not shown in the text box)
    };
    enum Overlay { NONE, MENU, SAVE, LOAD, SETTINGS, CONFIRM_EXIT, CHAPTERS };

    // graphics
    void computeGeometry();
    gfx::TexPtr loadTexture(vn::ResCat cat, const std::string& path, bool isBg, bool fullIfTiny = false);
    void ensureLoaded(Scene& s);
    void layerPos(const Layer& l, float& x, float& y) const;
    void drawLayer(const Scene& s, const Layer& l, float alpha, float dx, float dy);
    void drawBg(const Scene& s, float alpha, float dx, float dy, float grow);
    void drawScene(Scene& s, float alpha, float bgDx = 0, float spDx = 0, float dy = 0, float grow = 0);
    void drawPicture(float eye);
    void drawPreview(float eye, float ox);
    void drawTextPane(float w, float h, bool dimAll);
    void drawAdvBox(float dx);
    void drawChoices(float w, float avail, bool touch);
    void drawToast(float w, float y);
    // screen layout
    bool picOnTop() const { return layout_ == 0 || layout_ == 2; }
    bool paneOnTop() const { return layout_ == 1 || layout_ == 3; }
    bool advMode() const { return layout_ >= 2; }
    float paneW() const { return paneOnTop() ? (float)gfx::TOP_W : (float)gfx::BOT_W; }
    float paneH() const;
    float advTextW() const { return picW_ - 28; }
    int advLines() const;
    const Para* advPara() const;
    void wrapPara(Para& p);
    void loadNovelPrefs();
    void saveNovelPrefs();
    void beginFilm(const Scene& s);
    void endFilm();
    static bool sameLayer(const Layer& a, const Layer& b);
    // text
    void rewrap();
    int visibleLines() const;
    int totalLines() const;
    Para& addPara(const std::string& text, u32 color);
    bool revealing() const;
    void finishReveal();
    // flow
    void runScript(bool fast);
    void updateGame(const ui::Input& in);
    void updateOverlay(const ui::Input& in);
    void openOverlay(Overlay o);
    std::string slotPath(int slot) const;
    void doSave(int slot, bool silent = false);
    void autoSave();
    int newestSave(long long* stamp = nullptr);
    static int slotOf(int index);
    bool doLoad(int slot);
    void refreshSlots();
    void toast(const std::string& s);
    void applyAudioSettings();
    void resetForJump();
    int audioSlot(int kind, int channel) const;

    ui::App& app_;
    vn::Novel novel_;
    std::unique_ptr<vn::Engine> eng_;
    bool exit_ = false, ended_ = false;

    // stage geometry
    int stageW_ = 256, stageH_ = 192, refW_ = 0, refH_ = 0;
    float dispX_ = 0, dispY_ = 0, dispW_ = 400, dispH_ = 240, posSX_ = 1, posSY_ = 1, spriteKX_ = 1, spriteKY_ = 1;
    int fitUsed_ = 0;
    int layout_ = 0;                  // g_config.screenLayout in use
    float picW_ = 400, picH_ = 240;   // screen the picture is drawn on
    bool pinSprites_ = false;         // VNDS sprites stand on the bottom edge (per novel, stored in saves)
    bool hideBox_ = false;            // text box hidden (B) in one-screen layouts
    int paraSerial_ = 0, clearSerial_ = 0;
    struct CacheEnt {
        std::string key;
        gfx::TexPtr tex;
    };
    std::list<CacheEnt> texCache_;

    Scene shown_, next_, prev_;
    bool nextDirty_ = false, nextBgChanged_ = false;
    int nextFade_ = 16;
    int fadeLeft_ = 0, fadeTotal_ = 0;
    bool fadeBg_ = false;
    int shakeFrames_ = 0, shakeAmp_ = 0;
    float shakeX_ = 0, shakeY_ = 0;

    // text pane
    std::deque<Para> log_;
    bool paraOpen_ = false;  // last paragraph accepts inline text (Higurashi)
    int scrollBack_ = 0;     // lines scrolled up from the bottom
    float revealChars_ = 0;
    size_t revealTotal_ = 0;
    bool revealActive_ = false;
    int fontSizeUsed_ = 0;

    // choice
    std::vector<std::string> choices_;
    int choiceSel_ = 0;
    float choiceScroll_ = 0;

    // modes
    bool autoMode_ = false, skipSticky_ = false, skipping_ = false;
    int autoWait_ = 0;
    int frame_ = 0;

    // audio coalescing (VNDS music) and per-slot names (Higurashi BGM)
    std::string wantMusic_, curMusic_;
    bool musicDirty_ = false;
    std::string slotName_[8];

    // overlays
    Overlay overlay_ = NONE;
    ui::Menu menu_;
    std::vector<int> menuIds_;
    int slotSel_ = 0, slotScroll_ = 0, settingSel_ = 0, settingScroll_ = 0;
    int playFrames_ = 0;  // frames since the last autosave
    struct SlotInfo {
        bool used = false;
        std::string date, text;
        vn::SaveState state;
    };
    std::vector<SlotInfo> slots_;
    Scene preview_;
    int previewFor_ = -1;
    std::vector<std::string> listItems_;
    int listChapters_ = 0, listSel_ = 0, listScroll_ = 0;
    std::string toast_;
    int toastFrames_ = 0;
};
