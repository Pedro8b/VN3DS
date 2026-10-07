// VNDS script interpreter (.scr). Semantics follow VNDSx 1.4.9 so existing
// novels behave the same; unknown commands are skipped with a warning.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "vfs.h"

namespace vn {

struct Variable {
    enum Type { NUL, INT, STR } type = NUL;
    int i = 0;
    std::string s;
    static Variable fromLiteral(const std::string& v);
    static Variable ofInt(int v);
    static Variable ofStr(const std::string& v);
};
using VarMap = std::map<std::string, Variable>;

// Implemented by the frontend.
class ScriptHost {
public:
    virtual ~ScriptHost() {}
    virtual void setBackground(const std::string& path, int fadeFrames) = 0;  // also clears sprites
    virtual void addSprite(const std::string& path, int x, int y) = 0;
    // Show pending bg/sprite changes. Return true if an animated transition started;
    // the engine then pauses (WAIT_FX) until resume() is called, like VNDS on the DS.
    virtual bool flushGraphics(bool instant) = 0;
    virtual void playSound(const std::string& path, int repeats) = 0;  // "~" stops
    virtual void playMusic(const std::string& path) = 0;               // "~" stops
    virtual void appendText(const std::string& text) = 0;  // "" = empty line
    virtual void clearText(bool full) = 0;
    virtual void showChoice(const std::vector<std::string>& options) = 0;
    virtual void globalsChanged() = 0;
    virtual void scriptError(const std::string& msg) = 0;

    // ---- extended API (Higurashi-style engines); defaults keep plain VNDS hosts working
    // Like flushGraphics() but with an explicit transition length (0 = instant).
    virtual bool flushGraphicsFor(int frames) { return flushGraphics(frames <= 0); }
    // Continue the current paragraph instead of starting a new one ('\n' breaks lines).
    virtual void appendInline(const std::string& text) { appendText(text); }
    // Numbered sprite layers, positioned relative to the screen centre in a 640x480 space.
    virtual void setLayer(int layer, const std::string& path, int x, int y, int priority) { addSprite(path, x, y); }
    virtual void clearLayer(int layer) {}
    virtual void clearLayers() {}
    virtual void setBackgroundKeep(const std::string& path) { setBackground(path, 0); }  // keeps sprites
    // mode 0 = none, 1 = monochrome tint (r,g,b), 2 = negative. a = strength 0..255
    virtual void setFilm(int mode, int r, int g, int b, int a) {}
    virtual void shake(int frames, int amplitude) {}
    // kind: 0 = BGM, 1 = SE, 2 = voice. volume 0..1, loops -1 = forever
    virtual void playAudio(int kind, int channel, const std::string& path, float volume, int loops) {
        if (kind == 0) playMusic(path);
        else playSound(path, loops);
    }
    virtual void stopAudio(int kind, int channel, int fadeMs) {
        if (kind == 0) playMusic("~");
        else playSound("~", 0);
    }
    virtual bool audioPlaying(int kind, int channel) { return false; }
};

struct SpriteState {
    std::string path;
    int x = 0, y = 0;
};

struct SaveState {
    std::string file;       // "main.scr"
    int line = -1;          // command index (ours, exact)
    int textPos = 0;        // VNDS compatible: number of text commands before 'line'
    VarMap vars;
    std::string background, music, date, lastText;
    std::vector<SpriteState> sprites;
    long long stamp = 0;  // unix time of the save (to find the newest one)
    int pinSprites = -1;  // VN3DS display option stored with the save (-1 = not stored)
};

// Common interface of the script interpreters (VNDS .scr, Higurashi .txt/Lua).
class Engine {
public:
    enum State { RUNNING, WAIT_INPUT, WAIT_CHOICE, WAIT_DELAY, WAIT_FX, ENDED };
    virtual ~Engine() {}

    virtual bool start(const std::string& file = "") = 0;
    // Runs until something blocks. 'fast' = skip mode (no fades, no sounds).
    virtual State run(bool fast = false) = 0;
    virtual State state() const = 0;
    virtual void advance() = 0;              // user clicked while WAIT_INPUT
    virtual void choose(int index) = 0;      // 0-based option while WAIT_CHOICE
    virtual void tickDelay(int frames) = 0;  // WAIT_DELAY countdown
    virtual void cancelDelay() = 0;
    virtual void resume() = 0;               // transition finished (WAIT_FX -> RUNNING)
    virtual VarMap& globals() = 0;
    virtual SaveState save() const = 0;
    virtual bool load(const SaveState& s) = 0;
    virtual const std::string& currentFile() const = 0;
    virtual int currentLine() const = 0;
    virtual const std::string& lastText() const = 0;
    // Optional chapter / tips navigation (Higurashi presets).
    virtual std::vector<std::string> chapters() { return {}; }
    virtual bool jumpToChapter(int) { return false; }
    virtual std::vector<std::string> tips() { return {}; }
    virtual bool playTip(int) { return false; }
};

class ScriptEngine : public Engine {
public:
    ScriptEngine(Novel& novel, ScriptHost& host) : novel_(novel), host_(host) {}

    bool start(const std::string& file = "") override;
    State run(bool fast = false) override;
    State state() const override { return state_; }
    void advance() override;
    void choose(int index) override;
    int delayFrames() const { return delay_; }
    void tickDelay(int frames) override;
    void cancelDelay() override;
    void resume() override;

    VarMap& vars() { return vars_; }
    VarMap& globals() override { return globals_; }

    SaveState save() const override;
    bool load(const SaveState& s) override;

    const std::string& currentFile() const override { return file_; }
    int currentLine() const override { return pc_; }
    const std::string& lastText() const override { return lastText_; }

private:
    enum Op {
        C_SKIP, C_TEXT, C_BGLOAD, C_SETIMG, C_SOUND, C_MUSIC, C_CHOICE, C_SETVAR, C_GSETVAR, C_IF, C_FI,
        C_JUMP, C_DELAY, C_RANDOM, C_LABEL, C_GOTO, C_CLEARTEXT, C_ENDSCRIPT
    };
    struct Cmd {
        Op op = C_SKIP;
        std::vector<std::string> a;  // parsed arguments
    };

    bool loadFile(const std::string& file);
    void parseLine(const std::string& line, Cmd& c);
    void exec(const Cmd& c, bool fast);
    std::string replaceVars(const std::string& text) const;
    void setVar(VarMap& m, const std::string& name, char op, const std::string& value);
    bool evalIf(const std::string& a, const std::string& op, const std::string& b) const;
    bool gotoLabel(const std::string& label);
    bool flush(bool fast);  // true = yielded for a transition
    void endOfScript();

    Novel& novel_;
    ScriptHost& host_;
    std::string file_;
    std::vector<Cmd> cmds_;
    int pc_ = 0;          // next command to execute
    int curCmd_ = 0;      // index of the command that caused the current wait
    State state_ = ENDED;
    int delay_ = 0;
    VarMap vars_, globals_;
    std::string background_, music_, lastText_;
    std::vector<SpriteState> sprites_;
    bool bgPending_ = false;
    std::vector<std::string> warned_;
};

}  // namespace vn
