// Interpreter for Higurashi-Vita / MangaGamer style scripts (Scripts/*.txt with
// "function main() ... end"), run on embedded Lua 5.1. Engine calls such as
// OutputLine, DrawBustshot, PlayBGM are implemented in C++ and mapped onto the
// same ScriptHost the VNDS interpreter uses. Chapter order comes from the
// Higurashi-Vita preset file (includedPreset.txt).
#pragma once
#include <string>
#include <vector>

#include "script.h"

struct lua_State;

namespace vn {

class HigurashiEngine : public Engine {
public:
    HigurashiEngine(Novel& novel, ScriptHost& host);
    ~HigurashiEngine() override;

    bool start(const std::string& file = "") override;
    State run(bool fast = false) override;
    State state() const override { return state_; }
    void advance() override;
    void choose(int) override {}
    void tickDelay(int frames) override;
    void cancelDelay() override;
    void resume() override;
    VarMap& globals() override { return globals_; }
    SaveState save() const override;
    bool load(const SaveState& s) override;
    const std::string& currentFile() const override { return file_; }
    int currentLine() const override { return textPos_; }
    const std::string& lastText() const override { return lastText_; }

    std::vector<std::string> chapters() override;
    bool jumpToChapter(int idx) override;
    std::vector<std::string> tips() override;
    bool playTip(int idx) override;

    // ---- called from the Lua bindings
    int lOutputLine(lua_State* L, bool all);
    int lClearMessage(lua_State* L);
    int lWait(lua_State* L);
    int lDrawScene(lua_State* L, int nameArg, int timeArg, bool keepSprites);
    int lDrawBustshot(lua_State* L, bool filtering);
    int lDrawSprite(lua_State* L);
    int lFadeLayer(lua_State* L, bool all);
    int lFilm(lua_State* L, int mode);
    int lShake(lua_State* L, bool sx);
    int lPlay(lua_State* L, int kind);
    int lStop(lua_State* L, int kind, bool fade);
    int lFlag(lua_State* L, bool global, bool set);
    int lLoadScriptChunk(lua_State* L);

private:
    bool loadScript(const std::string& name);
    bool compile(const std::string& name, std::string& err);
    void nextScript();
    void resetPresentation();
    int yieldFor(lua_State* L, State st);
    int flushAndMaybeWait(lua_State* L, int ms, bool wait);
    int framesFor(int ms) const;
    bool quick() const { return fast_ || replaying_; }
    void parsePreset();
    void appendText(const std::string& s);

    Novel& novel_;
    ScriptHost& host_;
    lua_State* L_ = nullptr;
    lua_State* co_ = nullptr;
    int coRef_ = -2;  // LUA_NOREF
    bool started_ = false;

    State state_ = ENDED;
    bool fast_ = false;
    int delay_ = 0;
    std::string file_, lastText_;
    int textPos_ = 0;
    VarMap globals_, locals_;

    // preset
    std::vector<std::string> scripts_, tipScripts_, chapterNames_, tipNames_;
    int chapterIdx_ = -1;
    bool inTip_ = false;
    SaveState returnPoint_;

    // restoring a save = replaying the script quickly up to textPos
    bool replaying_ = false;
    int replayTarget_ = 0;
    std::string replayBgm_[8];
    bool replayBgmSet_[8] = {false};
    std::vector<std::pair<bool, std::string>> page_;  // (inline?, text) since last ClearMessage
};

}  // namespace vn
