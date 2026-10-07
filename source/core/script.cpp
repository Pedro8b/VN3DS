#include "script.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace vn {

Variable Variable::fromLiteral(const std::string& v) {
    Variable r;
    if (v.empty()) return r;
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        r.type = STR;
        r.s = v.substr(1, v.size() - 2);
    } else if (std::isdigit((unsigned char)v[0]) || v[0] == '-') {
        r.type = INT;
        r.i = atoi(v.c_str());
        r.s = std::to_string(r.i);
    } else {
        r.type = INT;
        r.i = 0;
        r.s = "0";
    }
    return r;
}

Variable Variable::ofInt(int v) {
    Variable r;
    r.type = INT;
    r.i = v;
    r.s = std::to_string(v);
    return r;
}

Variable Variable::ofStr(const std::string& v) {
    Variable r;
    r.type = STR;
    r.s = v;
    r.i = atoi(v.c_str());
    return r;
}

static std::vector<std::string> tokens(const std::string& s, size_t maxTokens, std::string* rest) {
    std::vector<std::string> out;
    size_t i = 0;
    while (out.size() < maxTokens) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    if (rest) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        *rest = s.substr(std::min(i, s.size()));
    }
    return out;
}

// Makes text display-ready: "\\n" escapes, Ren'Py text tags and (for scripts converted
// from Ren'Py) the quotes around every line and speaker variables.
static std::string cleanText(const std::string& in, bool renpy) {
    std::string s = in;
    if (renpy) {
        std::string t = trim(s);
        size_t q = t.find('"');
        if (q != std::string::npos && t.size() >= q + 2 && t.back() == '"') {
            std::string name = trim(t.substr(0, q));
            while (!name.empty() && (name.back() == ':' || name.back() == ' ')) name.pop_back();
            std::string body = t.substr(q + 1, t.size() - q - 2);
            bool var = !name.empty();  // lowercase ascii identifier = Ren'Py character variable
            for (char ch : name)
                if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) var = false;
            if (name.empty() || var) s = body;
            else if (name.size() <= 40) s = name + ": " + body;
        }
    }
    static const char* tags[] = {"i", "b", "u", "s", "plain", "color", "size", "font", "alpha", "cps", "k",
                                 "a", "w", "p", "nw", "fast", "rb", "rt", "space", "vspace", "image",
                                 "outlinecolor", "sc", "shader", "clear"};
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') {
                while (!o.empty() && o.back() == ' ') o.pop_back();
                o.push_back('\n');
                i++;
                while (i + 1 < s.size() && s[i + 1] == ' ') i++;
                continue;
            }
            if (n == '"' || n == '\\' || n == '\'') {
                o.push_back(n);
                i++;
                continue;
            }
            if (n == 't') {
                o.push_back(' ');
                i++;
                continue;
            }
        } else if (ch == '{') {
            if (i + 1 < s.size() && s[i + 1] == '{') {
                o.push_back('{');
                i++;
                continue;
            }
            size_t e = s.find('}', i);
            if (e != std::string::npos && e - i < 64) {
                std::string tag = s.substr(i + 1, e - i - 1);
                if (!tag.empty() && tag[0] == '/') tag.erase(0, 1);
                size_t eq = tag.find('=');
                if (eq != std::string::npos) tag.resize(eq);
                bool known = false;
                for (const char* k : tags)
                    if (tag == k) known = true;
                if (known) {
                    i = e;
                    continue;
                }
            }
        }
        o.push_back(ch);
    }
    return o;
}

void ScriptEngine::parseLine(const std::string& raw, Cmd& c) {
    std::string line = trim(raw);
    c.op = C_SKIP;
    c.a.clear();
    if (line.empty() || line[0] == '#') return;
    std::string rest;
    auto t = tokens(line, 1, &rest);
    const std::string& name = t[0];
    if (name == "text") {
        // Scripts converted from Ren'Py indent the text ("text      Name "Hello"") and
        // sometimes leak Ren'Py statements ("text label start:").
        size_t ws = 0;
        while (4 + ws < line.size() && (line[4 + ws] == ' ' || line[4 + ws] == '\t')) ws++;
        if (startsWith(rest, "label ") && !rest.empty() && rest.back() == ':' && rest.find('"') == std::string::npos) {
            c.op = C_LABEL;
            c.a = {trim(rest.substr(6, rest.size() - 7))};
            return;
        }
        c.op = C_TEXT;
        c.a = {rest, ws >= 2 ? "r" : ""};
    } else if (name == "bgload") {
        c.op = C_BGLOAD;
        auto a = tokens(rest, 2, nullptr);
        c.a = {a.size() > 0 ? a[0] : "", a.size() > 1 ? a[1] : "-1"};
    } else if (name == "setimg") {
        c.op = C_SETIMG;
        auto a = tokens(rest, 3, nullptr);
        while (a.size() < 3) a.push_back("0");
        c.a = a;
    } else if (name == "sound") {
        c.op = C_SOUND;
        auto a = tokens(rest, 2, nullptr);
        c.a = {a.size() > 0 ? a[0] : "~", a.size() > 1 ? a[1] : "1"};
    } else if (name == "music") {
        c.op = C_MUSIC;
        auto a = tokens(rest, 1, nullptr);
        c.a = {a.size() > 0 ? a[0] : "~"};
    } else if (name == "choice") {
        c.op = C_CHOICE;
        for (auto& o : split(rest, '|')) c.a.push_back(trim(o));
    } else if (name == "setvar" || name == "gsetvar") {
        c.op = name[0] == 'g' ? C_GSETVAR : C_SETVAR;
        std::string value;
        auto a = tokens(rest, 2, &value);
        while (a.size() < 2) a.push_back("");
        c.a = {a[0], a[1], trim(value)};
    } else if (name == "if") {
        c.op = C_IF;
        std::string value;
        auto a = tokens(rest, 2, &value);
        while (a.size() < 2) a.push_back("");
        c.a = {a[0], a[1], trim(value)};
    } else if (name == "fi") {
        c.op = C_FI;
    } else if (name == "jump") {
        c.op = C_JUMP;
        auto a = tokens(rest, 2, nullptr);
        c.a = {a.size() > 0 ? a[0] : "", a.size() > 1 ? a[1] : ""};
    } else if (name == "delay") {
        c.op = C_DELAY;
        c.a = {trim(rest)};
    } else if (name == "random") {
        c.op = C_RANDOM;
        auto a = tokens(rest, 3, nullptr);
        while (a.size() < 3) a.push_back("0");
        c.a = a;
    } else if (name == "label") {
        c.op = C_LABEL;
        auto a = tokens(rest, 1, nullptr);
        c.a = {a.empty() ? "" : a[0]};
    } else if (name == "goto") {
        c.op = C_GOTO;
        auto a = tokens(rest, 1, nullptr);
        c.a = {a.empty() ? "" : a[0]};
    } else if (name == "cleartext") {
        c.op = C_CLEARTEXT;
        c.a = {trim(rest)};
    } else if (name == "endscript") {
        c.op = C_ENDSCRIPT;
    } else {
        if (std::find(warned_.begin(), warned_.end(), name) == warned_.end()) {
            warned_.push_back(name);
            logf(LOG_WARN, "unknown script command '%s' (skipped)", name.c_str());
        }
    }
}

bool ScriptEngine::loadFile(const std::string& fileIn) {
    std::string file = normPath(trim(fileIn));
    std::string resolved;
    auto s = novel_.openRes(RES_SCRIPT, file, &resolved);
    if (!s) {
        host_.scriptError("Script not found: " + file);
        return false;
    }
    std::vector<uint8_t> data;
    s->readAll(data);
    size_t start = 0;
    if (data.size() >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        // UTF-16 -> UTF-8 (BMP only), for scripts saved by Windows Notepad.
        bool le = data[0] == 0xFF;
        std::vector<uint8_t> u8;
        for (size_t i = 2; i + 1 < data.size(); i += 2) {
            uint32_t cp = le ? (data[i] | (data[i + 1] << 8)) : ((data[i] << 8) | data[i + 1]);
            if (cp < 0x80) u8.push_back((uint8_t)cp);
            else if (cp < 0x800) {
                u8.push_back((uint8_t)(0xC0 | (cp >> 6)));
                u8.push_back((uint8_t)(0x80 | (cp & 0x3F)));
            } else {
                u8.push_back((uint8_t)(0xE0 | (cp >> 12)));
                u8.push_back((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
                u8.push_back((uint8_t)(0x80 | (cp & 0x3F)));
            }
        }
        data.swap(u8);
    } else if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        start = 3;
    }
    cmds_.clear();
    size_t i = start;
    std::string line;
    while (i < data.size()) {
        size_t e = i;
        while (e < data.size() && data[e] != '\n') e++;
        line.assign((const char*)data.data() + i, e - i);
        Cmd c;
        parseLine(line, c);
        cmds_.push_back(std::move(c));
        i = e + 1;
    }
    file_ = file;
    pc_ = 0;
    curCmd_ = 0;
    logf(LOG_INFO, "script %s (%u lines) <- %s", file.c_str(), (unsigned)cmds_.size(), resolved.c_str());
    return true;
}

bool ScriptEngine::start(const std::string& fileIn) {
    std::string file = fileIn.empty() ? novel_.mainScript() : fileIn;
    vars_.clear();
    sprites_.clear();
    background_.clear();
    music_.clear();
    lastText_.clear();
    bgPending_ = false;
    state_ = RUNNING;
    if (!loadFile(file)) {
        state_ = ENDED;
        return false;
    }
    return true;
}

std::string ScriptEngine::replaceVars(const std::string& text) const {
    if (text.find('$') == std::string::npos) return text;
    std::string out;
    size_t n = 0;
    while (n < text.size()) {
        bool brace = text[n] == '{' && n + 1 < text.size() && text[n + 1] == '$';
        if (text[n] != '$' && !brace) {
            out.push_back(text[n++]);
            continue;
        }
        if (text[n] == '$' && n + 1 < text.size() && text[n + 1] == '$') {
            out.push_back('$');
            n += 2;
            continue;
        }
        char endChar = ' ';
        if (brace) {
            endChar = '}';
            n++;
        }
        n++;  // '$'
        std::string name;
        while (n < text.size() && text[n] != '\n' && text[n] != endChar) name.push_back(text[n++]);
        if (brace && n < text.size()) n++;  // '}'
        auto g = globals_.find(name);
        if (g != globals_.end()) out += g->second.s;
        else {
            auto v = vars_.find(name);
            if (v != vars_.end()) out += v->second.s;
        }
    }
    return out;
}

void ScriptEngine::setVar(VarMap& m, const std::string& name, char op, const std::string& value) {
    if (op == '~') {
        m.clear();
        return;
    }
    Variable var = Variable::fromLiteral(value);
    auto vi = vars_.find(value);
    if (vi != vars_.end()) var = vi->second;
    else {
        auto gi = globals_.find(value);
        if (gi != globals_.end()) var = gi->second;
    }
    Variable target = m[name];
    bool str = target.type == Variable::STR || var.type == Variable::STR;
    if (!str) {
        target.type = Variable::INT;
        if (op == '+') target.i += var.i;
        else if (op == '-') target.i -= var.i;
        else if (op == '=') target.i = var.i;
        else {
            logf(LOG_WARN, "setvar: bad operator '%c'", op);
            return;
        }
        target.s = std::to_string(target.i);
    } else {
        target.type = Variable::STR;
        if (op == '+') target.s += var.s;
        else if (op == '=') target.s = var.s;
        else {
            logf(LOG_WARN, "setvar: bad operator '%c' for string", op);
            return;
        }
        target.i = atoi(target.s.c_str());
    }
    m[name] = target;
}

bool ScriptEngine::evalIf(const std::string& a, const std::string& op, const std::string& b) const {
    auto lookupVar = [&](const std::string& e) {
        Variable v = Variable::fromLiteral(e);
        auto vi = vars_.find(e);
        if (vi != vars_.end()) return vi->second;
        auto gi = globals_.find(e);
        if (gi != globals_.end()) return gi->second;
        return v;
    };
    Variable v1 = lookupVar(a), v2 = lookupVar(b);
    bool ints = v1.type == Variable::INT && v2.type == Variable::INT;
    int cmp = ints ? (v1.i < v2.i ? -1 : v1.i > v2.i ? 1 : 0) : strcmp(v1.s.c_str(), v2.s.c_str());
    if (op == "==") return cmp == 0;
    if (op == "!=") return cmp != 0;
    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;
    if (op == ">") return cmp > 0;
    if (op == "<") return cmp < 0;
    logf(LOG_WARN, "if: unknown operator '%s'", op.c_str());
    return false;
}

bool ScriptEngine::gotoLabel(const std::string& label) {
    for (size_t i = 0; i < cmds_.size(); i++) {
        if (cmds_[i].op == C_LABEL && cmds_[i].a[0] == label) {
            pc_ = (int)i + 1;
            return true;
        }
    }
    host_.scriptError("Label not found: " + label + " in " + file_);
    return false;
}

void ScriptEngine::endOfScript() {
    std::string mainScr = novel_.mainScript();
    if (iequals(baseName(file_), baseName(mainScr))) {
        state_ = ENDED;
        return;
    }
    if (!loadFile(mainScr)) state_ = ENDED;
}

bool ScriptEngine::flush(bool fast) {
    bgPending_ = false;
    if (host_.flushGraphics(fast) && !fast) {
        pc_ = curCmd_;  // re-run this command once the transition is over
        state_ = WAIT_FX;
        return true;
    }
    return false;
}

void ScriptEngine::resume() {
    if (state_ == WAIT_FX) state_ = RUNNING;
}

void ScriptEngine::exec(const Cmd& c, bool fast) {
    switch (c.op) {
        case C_SKIP:
        case C_FI:
        case C_LABEL:
            return;
        case C_TEXT: {
            if (flush(fast)) return;
            const std::string& t = c.a[0];
            char first = t.empty() ? 0 : t[0];
            if (first == '~') {
                host_.appendText("");
            } else if (first == '!') {
                state_ = WAIT_INPUT;
            } else {
                std::string s = cleanText(replaceVars(first == '@' ? t.substr(1) : t), c.a.size() > 1 && !c.a[1].empty());
                host_.appendText(s);
                if (!s.empty()) lastText_ = s;
                if (first != '@') state_ = WAIT_INPUT;
            }
            return;
        }
        case C_BGLOAD: {
            if (bgPending_ && flush(fast)) return;
            std::string p = replaceVars(c.a[0]);
            int fade = atoi(c.a[1].c_str());
            background_ = p;
            sprites_.clear();
            bgPending_ = true;
            host_.setBackground(p, fade < 0 ? 16 : fade);
            return;
        }
        case C_SETIMG: {
            SpriteState sp;
            sp.path = replaceVars(c.a[0]);
            sp.x = atoi(replaceVars(c.a[1]).c_str());
            sp.y = atoi(replaceVars(c.a[2]).c_str());
            sprites_.push_back(sp);
            host_.addSprite(sp.path, sp.x, sp.y);
            return;
        }
        case C_SOUND:
            if (!fast) host_.playSound(replaceVars(c.a[0]), atoi(c.a[1].c_str()));
            return;
        case C_MUSIC: {
            std::string p = replaceVars(c.a[0]);
            music_ = p == "~" ? "" : p;
            host_.playMusic(p);
            return;
        }
        case C_CHOICE: {
            if (flush(fast)) return;
            std::vector<std::string> opts;
            for (auto& o : c.a) opts.push_back(trim(cleanText(replaceVars(o), false)));
            host_.showChoice(opts);
            state_ = WAIT_CHOICE;
            return;
        }
        case C_SETVAR:
            setVar(vars_, c.a[0], c.a[1].empty() ? '=' : c.a[1][0], c.a[2]);
            return;
        case C_GSETVAR:
            setVar(globals_, c.a[0], c.a[1].empty() ? '=' : c.a[1][0], c.a[2]);
            host_.globalsChanged();
            return;
        case C_IF:
            if (!evalIf(c.a[0], c.a[1], c.a[2])) {
                int nest = 1;
                while (pc_ < (int)cmds_.size() && nest > 0) {
                    Op o = cmds_[pc_].op;
                    if (o == C_IF) nest++;
                    else if (o == C_FI) nest--;
                    pc_++;
                }
            }
            return;
        case C_JUMP: {
            std::string f = replaceVars(c.a[0]);
            std::string label = replaceVars(c.a[1]);
            if (!loadFile(f)) {
                state_ = WAIT_INPUT;  // let the user read the error, then continue
                return;
            }
            if (!label.empty()) gotoLabel(label);
            return;
        }
        case C_DELAY: {
            int n = atoi(c.a[0].c_str());
            if (!fast && n > 0) {
                if (flush(false)) return;
                delay_ = n;
                state_ = WAIT_DELAY;
            }
            return;
        }
        case C_RANDOM: {
            int lo = atoi(c.a[1].c_str()), hi = atoi(c.a[2].c_str());
            if (hi < lo) std::swap(lo, hi);
            int v = lo + rand() % (hi - lo + 1);
            setVar(vars_, c.a[0], '=', std::to_string(v));
            return;
        }
        case C_GOTO:
            gotoLabel(replaceVars(c.a[0]));
            return;
        case C_CLEARTEXT:
            host_.clearText(!c.a[0].empty() && c.a[0][0] == '!');
            return;
        case C_ENDSCRIPT:
            endOfScript();
            return;
    }
}

ScriptEngine::State ScriptEngine::run(bool fast) {
    if (state_ == ENDED) return state_;
    if (state_ != RUNNING) return state_;
    for (int budget = 0; budget < 5000; budget++) {
        if (pc_ >= (int)cmds_.size()) {
            endOfScript();
            if (state_ == ENDED) {
                host_.flushGraphics(true);
                return state_;
            }
            continue;
        }
        curCmd_ = pc_;
        const Cmd& c = cmds_[pc_++];
        exec(c, fast);
        if (state_ != RUNNING) return state_;
    }
    return state_;  // still RUNNING: give the frontend a frame
}

void ScriptEngine::advance() {
    if (state_ == WAIT_INPUT) state_ = RUNNING;
}

void ScriptEngine::choose(int index) {
    if (state_ != WAIT_CHOICE) return;
    setVar(vars_, "selected", '=', std::to_string(index + 1));
    state_ = RUNNING;
}

void ScriptEngine::tickDelay(int frames) {
    if (state_ != WAIT_DELAY) return;
    delay_ -= frames;
    if (delay_ <= 0) {
        delay_ = 0;
        state_ = RUNNING;
    }
}

void ScriptEngine::cancelDelay() {
    if (state_ == WAIT_DELAY) {
        delay_ = 0;
        state_ = RUNNING;
    }
}

SaveState ScriptEngine::save() const {
    SaveState s;
    s.file = file_;
    s.line = curCmd_;
    int texts = 0;
    for (int i = 0; i < curCmd_ && i < (int)cmds_.size(); i++)
        if (cmds_[i].op == C_TEXT) texts++;
    s.textPos = texts;
    s.vars = vars_;
    s.background = background_;
    s.sprites = sprites_;
    s.music = music_;
    s.lastText = lastText_;
    return s;
}

bool ScriptEngine::load(const SaveState& s) {
    std::string f = s.file;
    // VNDS saves store "script/xxx.scr"
    if (startsWith(toLower(f), "script/")) f = f.substr(7);
    vars_ = s.vars;
    if (!loadFile(f.empty() ? novel_.mainScript() : f)) {
        state_ = ENDED;
        return false;
    }
    if (s.line >= 0 && s.line < (int)cmds_.size()) {
        pc_ = s.line;
    } else {
        // VNDS save: resume at the (textPos)-th text command of the file
        int texts = 0;
        pc_ = 0;
        for (int i = 0; i < (int)cmds_.size(); i++) {
            if (cmds_[i].op == C_TEXT) {
                if (texts == s.textPos) {
                    pc_ = i;
                    break;
                }
                texts++;
            }
        }
    }
    host_.clearText(true);
    host_.playSound("~", 0);
    std::string bg = s.background;
    if (startsWith(toLower(bg), "background/")) bg = bg.substr(11);
    background_ = bg;
    host_.setBackground(bg, 0);
    sprites_.clear();
    for (auto sp : s.sprites) {
        if (startsWith(toLower(sp.path), "foreground/")) sp.path = sp.path.substr(11);
        sprites_.push_back(sp);
        host_.addSprite(sp.path, sp.x, sp.y);
    }
    host_.flushGraphics(true);
    bgPending_ = false;
    std::string m = s.music;
    if (startsWith(toLower(m), "sound/")) m = m.substr(6);
    music_ = m;
    host_.playMusic(m.empty() ? "~" : m);
    lastText_ = s.lastText;
    state_ = RUNNING;
    return true;
}

}  // namespace vn
