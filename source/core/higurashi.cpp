#include "higurashi.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <lua5.1/lauxlib.h>
#include <lua5.1/lua.h>
#include <lua5.1/lualib.h>
}

namespace vn {

// ---------------------------------------------------------------------------
// helpers

static HigurashiEngine* eng(lua_State* L) { return (HigurashiEngine*)lua_touserdata(L, lua_upvalueindex(1)); }

static std::string argStr(lua_State* L, int i) {
    if (i < 1 || i > lua_gettop(L)) return "";
    if (lua_type(L, i) == LUA_TSTRING || lua_type(L, i) == LUA_TNUMBER) return lua_tostring(L, i);
    return "";
}
static int argInt(lua_State* L, int i, int def = 0) {
    if (i < 1 || i > lua_gettop(L)) return def;
    if (lua_type(L, i) == LUA_TNUMBER) return (int)lua_tonumber(L, i);
    if (lua_type(L, i) == LUA_TBOOLEAN) return lua_toboolean(L, i);
    return def;
}
static bool argBool(lua_State* L, int i, bool def = false) {
    if (i < 1 || i > lua_gettop(L)) return def;
    if (lua_type(L, i) == LUA_TBOOLEAN) return lua_toboolean(L, i) != 0;
    if (lua_type(L, i) == LUA_TNUMBER) return lua_tonumber(L, i) != 0;
    return def;
}
// Most drawing calls end with (..., time, wait). Returns time and sets wait.
static int tailTime(lua_State* L, bool& wait, int fallbackTimeArg = -1) {
    int n = lua_gettop(L);
    if (n >= 1 && lua_type(L, n) == LUA_TBOOLEAN) {
        wait = lua_toboolean(L, n) != 0;
        return argInt(L, n - 1);
    }
    wait = true;
    return fallbackTimeArg > 0 ? argInt(L, fallbackTimeArg) : 0;
}

static std::string stripTags(const std::string& s) {
    std::string o;
    bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>' && tag) tag = false;
        else if (!tag) o.push_back(c);
    }
    return o;
}

// Higurashi-Vita scripts are Lua with C-style comments / operators.
static std::string preprocess(const std::string& src) {
    std::string o;
    o.reserve(src.size() + 64);
    size_t i = 0;
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF)
        i = 3;
    char quote = 0;
    for (; i < src.size(); i++) {
        char c = src[i];
        char n = i + 1 < src.size() ? src[i + 1] : 0;
        if (quote) {
            o.push_back(c);
            if (c == '\\' && n) {
                o.push_back(n);
                i++;
            } else if (c == quote || c == '\n') {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            o.push_back(c);
        } else if (c == '-' && n == '-') {
            // already a Lua comment: copy to end of line
            while (i < src.size() && src[i] != '\n') o.push_back(src[i++]);
            if (i < src.size()) o.push_back('\n');
        } else if (c == '/' && n == '/') {
            o += "--";
            i++;
            while (i + 1 < src.size() && src[i + 1] != '\n') o.push_back(src[++i]);
        } else if (c == '!' && n == '=') {
            o += "~=";
            i++;
        } else if (c == '&' && n == '&') {
            o += " and ";
            i++;
        } else if (c == '|' && n == '|') {
            o += " or ";
            i++;
        } else {
            o.push_back(c);
        }
    }
    return o;
}

static const char* kPrelude = R"LUA(
Line_ContinueAfterTyping = 0
Line_WaitForInput = 1
Line_Normal = 2
TRUE = true
FALSE = false
TEXTMODE_NVL = 0
TEXTMODE_ADV = 1
function CallScript(name)
  local f = _LoadScriptChunk(name)
  if f then
    f()
    if main then main() end
  end
end
JumpScript = CallScript
local warned = {}
setmetatable(_G, { __index = function(t, k)
  if type(k) ~= "string" then return nil end
  if k == "NULL" then return nil end
  if string.match(k, "^G%u") then return k end
  if string.match(k, "^%u") then
    if not warned[k] then warned[k] = true; _Unknown(k) end
    return function() return 0 end
  end
  return nil
end })
)LUA";

// ---------------------------------------------------------------------------
// bindings

#define BIND(name, call) \
    static int f_##name(lua_State* L) { return eng(L)->call; }

BIND(OutputLine, lOutputLine(L, false))
BIND(OutputLineAll, lOutputLine(L, true))
BIND(ClearMessage, lClearMessage(L))
BIND(Wait, lWait(L))
BIND(DrawScene, lDrawScene(L, 1, 2, false))
BIND(DrawSceneWithMask, lDrawScene(L, 1, 5, false))
BIND(ChangeScene, lDrawScene(L, 1, 3, false))
BIND(DrawBG, lDrawScene(L, 1, 2, true))
BIND(FadeBG, lDrawScene(L, 0, 1, true))
BIND(DrawBustshot, lDrawBustshot(L, false))
BIND(DrawBustshotWithFiltering, lDrawBustshot(L, true))
BIND(DrawSprite, lDrawSprite(L))
BIND(FadeBustshot, lFadeLayer(L, false))
BIND(FadeAllBustshots, lFadeLayer(L, true))
BIND(DrawFilm, lFilm(L, 1))
BIND(FadeFilm, lFilm(L, 0))
BIND(Negative, lFilm(L, 2))
BIND(ShakeScreen, lShake(L, false))
BIND(ShakeScreenSx, lShake(L, true))
BIND(PlayBGM, lPlay(L, 0))
BIND(PlaySE, lPlay(L, 1))
BIND(PlayVoice, lPlay(L, 2))
BIND(StopBGM, lStop(L, 0, false))
BIND(FadeOutBGM, lStop(L, 0, true))
BIND(StopSE, lStop(L, 1, false))
BIND(FadeOutSE, lStop(L, 1, true))
BIND(StopVoice, lStop(L, 2, false))
BIND(GetGlobalFlag, lFlag(L, true, false))
BIND(SetGlobalFlag, lFlag(L, true, true))
BIND(GetLocalFlag, lFlag(L, false, false))
BIND(SetLocalFlag, lFlag(L, false, true))
BIND(_LoadScriptChunk, lLoadScriptChunk(L))

static int f_noop(lua_State*) { return 0; }
static int f_Unknown(lua_State* L) {
    logf(LOG_INFO, "higurashi: unimplemented function %s (ignored)", lua_tostring(L, 1));
    return 0;
}

static const struct {
    const char* name;
    lua_CFunction fn;
} kFuncs[] = {
    {"OutputLine", f_OutputLine},
    {"OutputLineAll", f_OutputLineAll},
    {"ClearMessage", f_ClearMessage},
    {"Wait", f_Wait},
    {"DrawScene", f_DrawScene},
    {"DrawSceneWithMask", f_DrawSceneWithMask},
    {"ChangeScene", f_ChangeScene},
    {"DrawBG", f_DrawBG},
    {"FadeBG", f_FadeBG},
    {"DrawBustshot", f_DrawBustshot},
    {"DrawBustshotWithFiltering", f_DrawBustshotWithFiltering},
    {"DrawSprite", f_DrawSprite},
    {"DrawSpriteWithFiltering", f_DrawSprite},
    {"FadeBustshot", f_FadeBustshot},
    {"FadeBustshotWithFiltering", f_FadeBustshot},
    {"FadeSprite", f_FadeBustshot},
    {"FadeSpriteWithFiltering", f_FadeBustshot},
    {"FadeAllBustshots", f_FadeAllBustshots},
    {"FadeAllSprites", f_FadeAllBustshots},
    {"DrawFilm", f_DrawFilm},
    {"FadeFilm", f_FadeFilm},
    {"Negative", f_Negative},
    {"ShakeScreen", f_ShakeScreen},
    {"ShakeScreenSx", f_ShakeScreenSx},
    {"PlayBGM", f_PlayBGM},
    {"PlaySE", f_PlaySE},
    {"PlayVoice", f_PlayVoice},
    {"StopBGM", f_StopBGM},
    {"FadeOutBGM", f_FadeOutBGM},
    {"StopSE", f_StopSE},
    {"FadeOutSE", f_FadeOutSE},
    {"StopVoice", f_StopVoice},
    {"GetGlobalFlag", f_GetGlobalFlag},
    {"SetGlobalFlag", f_SetGlobalFlag},
    {"GetLocalFlag", f_GetLocalFlag},
    {"SetLocalFlag", f_SetLocalFlag},
    {"_LoadScriptChunk", f__LoadScriptChunk},
    {"_Unknown", f_Unknown},
    // known no-ops (window / input / font settings that don't matter on the 3DS)
    {"DisableWindow", f_noop},
    {"EnableWindow", f_noop},
    {"SetValidityOfInput", f_noop},
    {"SetValidityOfSaving", f_noop},
    {"SetValidityOfLoading", f_noop},
    {"SetValidityOfSkipping", f_noop},
    {"SetValidityOfTextFade", f_noop},
    {"SetValidityOfWindowDisablingWhenGraphicsControl", f_noop},
    {"SetSpeedOfMessage", f_noop},
    {"SetDrawingPointOfMessage", f_noop},
    {"SetStyleOfMessageSwinging", f_noop},
    {"SetFontOfMessage", f_noop},
    {"EnableJumpingOfReturnIcon", f_noop},
    {"ActivateScreenEffectForcedly", f_noop},
    {"GetAchievement", f_noop},
    {"OptionsSetTextMode", f_noop},
    {"OptionsLoadADVBox", f_noop},
    {"SetGuiPosition", f_noop},
};

// ---------------------------------------------------------------------------

HigurashiEngine::HigurashiEngine(Novel& novel, ScriptHost& host) : novel_(novel), host_(host) {
    L_ = luaL_newstate();
    luaL_openlibs(L_);
    for (auto& f : kFuncs) {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, f.fn, 1);
        lua_setglobal(L_, f.name);
    }
    if (luaL_loadbuffer(L_, kPrelude, strlen(kPrelude), "prelude") || lua_pcall(L_, 0, 0, 0)) {
        logf(LOG_ERROR, "higurashi prelude: %s", lua_tostring(L_, -1));
        lua_pop(L_, 1);
    }
    globals_["GLanguage"] = Variable::ofInt(1);  // show the translated text column
    parsePreset();
    // Optional per-game init (_GameSpecific.lua); errors are harmless.
    std::string err;
    std::string gs;
    if (auto s = novel_.openRes(RES_SCRIPT, "_GameSpecific.lua")) {
        std::vector<uint8_t> d;
        s->readAll(d);
        gs = preprocess(std::string(d.begin(), d.end()));
        if (luaL_loadbuffer(L_, gs.data(), gs.size(), "_GameSpecific") || lua_pcall(L_, 0, 0, 0)) {
            logf(LOG_WARN, "_GameSpecific.lua: %s", lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    }
}

HigurashiEngine::~HigurashiEngine() {
    if (L_) lua_close(L_);
}

void HigurashiEngine::parsePreset() {
    std::string inc, txt;
    if (novel_.readRootText("includedPreset.txt", inc) && novel_.readRootText(trim(split(inc, '\n')[0]), txt)) {
        std::vector<std::string> lines;
        for (auto& l : split(txt, '\n')) {
            std::string t = trim(l);
            if (!t.empty()) lines.push_back(t);
        }
        size_t i = 0;
        auto readList = [&](std::vector<std::string>& out) {
            if (i >= lines.size()) return;
            int n = atoi(lines[i++].c_str());
            for (int k = 0; k < n && i < lines.size(); k++) out.push_back(lines[i++]);
        };
        readList(scripts_);
        readList(tipScripts_);
        for (; i < lines.size(); i++) {
            if (lines[i] == "tipnames") {
                i++;
                readList(tipNames_);
                i--;
            } else if (lines[i] == "chapternames") {
                i++;
                readList(chapterNames_);
                i--;
            }
        }
        logf(LOG_INFO, "preset: %u scripts, %u tips", (unsigned)scripts_.size(), (unsigned)tipScripts_.size());
    }
    if (scripts_.empty()) {
        for (auto& s : novel_.listScripts()) {
            if (fileExt(s) != "txt" || s[0] == '_') continue;
            std::string stem = stripExt(s);
            if (stem.find("tips") != std::string::npos) tipScripts_.push_back(stem);
            else scripts_.push_back(stem);
        }
    }
}

std::vector<std::string> HigurashiEngine::chapters() {
    return chapterNames_.size() == scripts_.size() ? chapterNames_ : scripts_;
}

std::vector<std::string> HigurashiEngine::tips() {
    return tipNames_.size() == tipScripts_.size() ? tipNames_ : tipScripts_;
}

bool HigurashiEngine::compile(const std::string& name, std::string& err) {
    std::string fn = fileExt(name) == "txt" ? name : name + ".txt";
    auto s = novel_.openRes(RES_SCRIPT, fn);
    if (!s) {
        err = "script not found: " + fn;
        return false;
    }
    std::vector<uint8_t> d;
    s->readAll(d);
    std::string src = preprocess(std::string(d.begin(), d.end()));
    if (luaL_loadbuffer(L_, src.data(), src.size(), name.c_str()) != 0) {
        err = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool HigurashiEngine::loadScript(const std::string& name) {
    std::string err;
    lua_pushnil(L_);
    lua_setglobal(L_, "main");
    if (!compile(name, err) || lua_pcall(L_, 0, 0, 0) != 0) {
        if (err.empty()) {
            err = lua_tostring(L_, -1);
            lua_pop(L_, 1);
        }
        host_.scriptError(err);
        return false;
    }
    if (coRef_ != LUA_NOREF && coRef_ != -2) luaL_unref(L_, LUA_REGISTRYINDEX, coRef_);
    co_ = lua_newthread(L_);
    coRef_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    lua_getglobal(co_, "main");
    if (!lua_isfunction(co_, -1)) {
        lua_pop(co_, 1);
        host_.scriptError(name + ": no main() function");
        return false;
    }
    file_ = name;
    textPos_ = 0;
    state_ = RUNNING;
    logf(LOG_INFO, "higurashi: running %s", name.c_str());
    return true;
}

bool HigurashiEngine::start(const std::string& file) {
    inTip_ = false;
    replaying_ = false;
    if (!file.empty()) {
        chapterIdx_ = -1;
        for (size_t i = 0; i < scripts_.size(); i++)
            if (iequals(scripts_[i], stripExt(file))) chapterIdx_ = (int)i;
        if (loadScript(stripExt(file))) return true;
        state_ = ENDED;
        return false;
    }
    if (scripts_.empty()) {
        host_.scriptError("no scripts found");
        state_ = ENDED;
        return false;
    }
    chapterIdx_ = 0;
    while (chapterIdx_ < (int)scripts_.size() && !loadScript(scripts_[chapterIdx_])) chapterIdx_++;
    if (chapterIdx_ >= (int)scripts_.size()) {
        state_ = ENDED;
        return false;
    }
    return true;
}

void HigurashiEngine::nextScript() {
    if (replaying_) {
        // save pointed past the end of the script: just show what we have
        replaying_ = false;
        host_.flushGraphicsFor(0);
    }
    if (inTip_) {
        inTip_ = false;
        SaveState back = returnPoint_;
        if (!back.file.empty() && load(back)) return;
    }
    while (chapterIdx_ >= 0 && chapterIdx_ + 1 < (int)scripts_.size()) {
        chapterIdx_++;
        if (loadScript(scripts_[chapterIdx_])) return;
    }
    state_ = ENDED;
}

HigurashiEngine::State HigurashiEngine::run(bool fast) {
    for (int guard = 0; guard < 4 && state_ == RUNNING; guard++) {
        fast_ = fast;
        int r = lua_resume(co_, 0);
        if (r == LUA_YIELD) return state_;
        if (r != 0) {
            std::string msg = lua_tostring(co_, -1) ? lua_tostring(co_, -1) : "error";
            host_.scriptError(file_ + ": " + msg);
        }
        nextScript();
    }
    return state_;
}

void HigurashiEngine::advance() {
    if (state_ == WAIT_INPUT) state_ = RUNNING;
}
void HigurashiEngine::tickDelay(int frames) {
    if (state_ != WAIT_DELAY) return;
    delay_ -= frames;
    if (delay_ <= 0) state_ = RUNNING;
}
void HigurashiEngine::cancelDelay() {
    if (state_ == WAIT_DELAY) {
        delay_ = 0;
        state_ = RUNNING;
    }
}
void HigurashiEngine::resume() {
    if (state_ == WAIT_FX) state_ = RUNNING;
}

int HigurashiEngine::yieldFor(lua_State* L, State st) {
    state_ = st;
    return lua_yield(L, 0);
}

int HigurashiEngine::framesFor(int ms) const { return ms <= 0 ? 0 : (ms * 60 + 999) / 1000; }

int HigurashiEngine::flushAndMaybeWait(lua_State* L, int ms, bool wait) {
    int frames = quick() ? 0 : framesFor(ms);
    bool started = host_.flushGraphicsFor(frames);
    if (started && wait) return yieldFor(L, WAIT_FX);
    return 0;
}

void HigurashiEngine::appendText(const std::string& s) {
    if (replaying_) page_.emplace_back(true, s);
    else host_.appendInline(s);
}

// ---------------------------------------------------------------------------

int HigurashiEngine::lOutputLine(lua_State* L, bool all) {
    std::string text, name;
    int mode;
    if (all) {
        name = argStr(L, 1);
        text = argStr(L, 2);
        mode = argInt(L, 3, 0);
    } else {
        std::string tr = argStr(L, 4), jp = argStr(L, 2);
        bool translated = !(globals_.count("GLanguage") && globals_["GLanguage"].i == 0);
        text = translated ? (tr.empty() ? jp : tr) : (jp.empty() ? tr : jp);
        name = translated ? argStr(L, 3) : argStr(L, 1);
        mode = argInt(L, 5, 0);
    }
    text = stripTags(text);
    if (!text.empty()) {
        appendText(text);
        std::string t = trim(text);
        if (!t.empty()) lastText_ = t;
    }
    if (mode == 1 || mode == 2) {
        textPos_++;
        if (replaying_) {
            if (textPos_ < replayTarget_) return 0;
            // reached the saved line: show the rebuilt page and continue normally
            replaying_ = false;
            host_.clearText(true);
            for (auto& p : page_) host_.appendInline(p.second);
            page_.clear();
            for (int ch = 0; ch < 8; ch++) {
                if (!replayBgmSet_[ch]) continue;
                if (replayBgm_[ch].empty()) host_.stopAudio(0, ch, 0);
                else host_.playAudio(0, ch, replayBgm_[ch], 1.0f, -1);
            }
            host_.flushGraphicsFor(0);
        }
        return yieldFor(L, WAIT_INPUT);
    }
    return 0;
}

int HigurashiEngine::lClearMessage(lua_State*) {
    if (replaying_) page_.clear();
    else host_.clearText(false);
    return 0;
}

int HigurashiEngine::lWait(lua_State* L) {
    if (quick()) return 0;
    delay_ = framesFor(argInt(L, 1));
    if (delay_ <= 0) return 0;
    return yieldFor(L, WAIT_DELAY);
}

int HigurashiEngine::lDrawScene(lua_State* L, int nameArg, int timeArg, bool keepSprites) {
    std::string name = nameArg > 0 ? argStr(L, nameArg) : std::string();
    bool wait = true;
    int ms = argInt(L, timeArg);
    if (keepSprites && lua_gettop(L) >= timeArg + 1) wait = argBool(L, timeArg + 1, true);
    if (keepSprites) host_.setBackgroundKeep(name);
    else host_.setBackground(name, framesFor(ms));
    return flushAndMaybeWait(L, ms, wait);
}

int HigurashiEngine::lDrawBustshot(lua_State* L, bool filtering) {
    int layer = argInt(L, 1);
    std::string name = argStr(L, 2);
    int x = argInt(L, filtering ? 5 : 3), y = argInt(L, filtering ? 6 : 4);
    bool wait;
    int ms = tailTime(L, wait);
    int n = lua_gettop(L);
    int priority = argInt(L, n - 2, layer);
    host_.setLayer(layer, name, x, y, priority);
    return flushAndMaybeWait(L, ms, wait);
}

int HigurashiEngine::lDrawSprite(lua_State* L) {
    int layer = argInt(L, 1);
    std::string name = argStr(L, 2);
    int x = argInt(L, 4), y = argInt(L, 5);
    bool wait;
    int ms = tailTime(L, wait);
    int n = lua_gettop(L);
    int priority = argInt(L, n - 2, layer);
    host_.setLayer(layer, name, x, y, priority);
    return flushAndMaybeWait(L, ms, wait);
}

int HigurashiEngine::lFadeLayer(lua_State* L, bool all) {
    bool wait;
    int ms = tailTime(L, wait, all ? 1 : 2);
    if (all) host_.clearLayers();
    else host_.clearLayer(argInt(L, 1));
    return flushAndMaybeWait(L, ms, wait);
}

int HigurashiEngine::lFilm(lua_State* L, int mode) {
    bool wait;
    int ms = tailTime(L, wait, mode == 1 ? 7 : 1);
    if (mode == 1) host_.setFilm(1, argInt(L, 2, 255), argInt(L, 3, 255), argInt(L, 4, 255), argInt(L, 5, 255));
    else if (mode == 2) host_.setFilm(2, 0, 0, 0, 255);
    else host_.setFilm(0, 0, 0, 0, 0);
    return flushAndMaybeWait(L, ms, wait);
}

int HigurashiEngine::lShake(lua_State* L, bool sx) {
    if (quick()) return 0;
    if (sx) host_.shake(18, 2 + 2 * argInt(L, 2, 1));
    else host_.shake(std::min(90, 8 * std::max(1, argInt(L, 4, 3))), 4);
    return 0;
}

int HigurashiEngine::lPlay(lua_State* L, int kind) {
    int ch = argInt(L, 1);
    std::string name = argStr(L, 2);
    float vol = std::max(0.0f, std::min(1.0f, argInt(L, 3, 128) / 128.0f));
    if (kind == 0) {
        if (replaying_) {
            if (ch >= 0 && ch < 8) {
                replayBgm_[ch] = name;
                replayBgmSet_[ch] = true;
            }
            return 0;
        }
        host_.playAudio(0, ch, name, vol, -1);
        return 0;
    }
    if (quick()) return 0;
    host_.playAudio(kind, ch, name, vol, 1);
    return 0;
}

int HigurashiEngine::lStop(lua_State* L, int kind, bool fade) {
    int ch = argInt(L, 1);
    int ms = fade ? argInt(L, 2) : 0;
    bool wait = fade && argBool(L, 3, false);
    if (kind == 0 && replaying_) {
        if (ch >= 0 && ch < 8) {
            replayBgm_[ch].clear();
            replayBgmSet_[ch] = true;
        }
        return 0;
    }
    host_.stopAudio(kind, ch, quick() ? 0 : ms);
    if (wait && !quick() && ms > 0) {
        delay_ = framesFor(ms);
        return yieldFor(L, WAIT_DELAY);
    }
    return 0;
}

int HigurashiEngine::lFlag(lua_State* L, bool global, bool set) {
    std::string key = argStr(L, 1);
    VarMap& m = global ? globals_ : locals_;
    if (set) {
        int v = argInt(L, 2);
        m[key] = Variable::ofInt(v);
        if (global) host_.globalsChanged();
        return 0;
    }
    auto it = m.find(key);
    lua_pushnumber(L, it == m.end() ? 0 : it->second.i);
    return 1;
}

int HigurashiEngine::lLoadScriptChunk(lua_State* L) {
    std::string err;
    if (!compile(argStr(L, 1), err)) {
        host_.scriptError(err);
        lua_pushnil(L);
        return 1;
    }
    // compile() left the chunk on L_'s stack; move it to the calling thread
    lua_xmove(L_, L, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// navigation & saves

void HigurashiEngine::resetPresentation() {
    host_.clearText(true);
    host_.clearLayers();
    host_.setFilm(0, 0, 0, 0, 0);
    host_.setBackground("", 0);
    host_.flushGraphicsFor(0);
    for (int ch = 0; ch < 3; ch++) host_.stopAudio(0, ch, 0);
    for (int ch = 0; ch < 8; ch++) host_.stopAudio(1, ch, 0);
    host_.stopAudio(2, 0, 0);
}

bool HigurashiEngine::jumpToChapter(int idx) {
    if (idx < 0 || idx >= (int)scripts_.size()) return false;
    inTip_ = false;
    replaying_ = false;
    resetPresentation();
    chapterIdx_ = idx;
    return loadScript(scripts_[idx]);
}

bool HigurashiEngine::playTip(int idx) {
    if (idx < 0 || idx >= (int)tipScripts_.size()) return false;
    if (!inTip_ && state_ != ENDED) returnPoint_ = save();
    inTip_ = true;
    replaying_ = false;
    resetPresentation();
    return loadScript(tipScripts_[idx]);
}

SaveState HigurashiEngine::save() const {
    SaveState s;
    s.file = file_;
    s.line = -1;
    s.textPos = textPos_;
    s.vars = locals_;
    s.lastText = lastText_;
    return s;
}

bool HigurashiEngine::load(const SaveState& s) {
    std::string f = stripExt(s.file);
    if (startsWith(toLower(f), "script/")) f = f.substr(7);
    chapterIdx_ = -1;
    for (size_t i = 0; i < scripts_.size(); i++)
        if (iequals(scripts_[i], f)) chapterIdx_ = (int)i;
    locals_ = s.vars;
    resetPresentation();
    replaying_ = s.textPos > 0;
    replayTarget_ = s.textPos;
    page_.clear();
    for (int ch = 0; ch < 8; ch++) {
        replayBgm_[ch].clear();
        replayBgmSet_[ch] = false;
    }
    if (!loadScript(f)) {
        replaying_ = false;
        state_ = ENDED;
        return false;
    }
    lastText_ = s.lastText;
    return true;
}

}  // namespace vn
