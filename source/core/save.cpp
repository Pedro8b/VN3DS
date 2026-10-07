#include "save.h"

#include <cstring>
#include <ctime>
#include <memory>

namespace vn {

namespace {

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            static const char* ents[][2] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
            bool done = false;
            for (auto& e : ents) {
                size_t n = strlen(e[0]);
                if (s.compare(i, n, e[0]) == 0) {
                    o += e[1];
                    i += n - 1;
                    done = true;
                    break;
                }
            }
            if (done) continue;
        }
        o.push_back(s[i]);
    }
    return o;
}

// Minimal XML DOM, enough for VNDS save files.
struct Node {
    std::string tag, text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<Node>> kids;
    const Node* child(const char* t) const {
        for (auto& k : kids)
            if (k->tag == t) return k.get();
        return nullptr;
    }
    std::string attr(const char* n) const {
        for (auto& a : attrs)
            if (a.first == n) return a.second;
        return "";
    }
    std::string childText(const char* t) const {
        const Node* c = child(t);
        return c ? trim(c->text) : "";
    }
};

struct Parser {
    const std::string& s;
    size_t p = 0;
    explicit Parser(const std::string& str) : s(str) {}
    void ws() {
        while (p < s.size() && isspace((unsigned char)s[p])) p++;
    }
    std::unique_ptr<Node> parse() {
        // skip prolog / comments
        for (;;) {
            ws();
            if (s.compare(p, 4, "<!--") == 0) {
                size_t e = s.find("-->", p);
                p = e == std::string::npos ? s.size() : e + 3;
            } else if (s.compare(p, 2, "<?") == 0) {
                size_t e = s.find("?>", p);
                p = e == std::string::npos ? s.size() : e + 2;
            } else break;
        }
        if (p >= s.size() || s[p] != '<') return nullptr;
        p++;
        auto n = std::unique_ptr<Node>(new Node());
        while (p < s.size() && !isspace((unsigned char)s[p]) && s[p] != '>' && s[p] != '/') n->tag.push_back(s[p++]);
        for (;;) {
            ws();
            if (p >= s.size()) return n;
            if (s[p] == '/') {
                p = s.find('>', p);
                p = p == std::string::npos ? s.size() : p + 1;
                return n;
            }
            if (s[p] == '>') {
                p++;
                break;
            }
            std::string k, v;
            while (p < s.size() && s[p] != '=' && !isspace((unsigned char)s[p]) && s[p] != '>') k.push_back(s[p++]);
            ws();
            if (p < s.size() && s[p] == '=') {
                p++;
                ws();
                char q = s[p];
                if (q == '"' || q == '\'') {
                    size_t e = s.find(q, p + 1);
                    if (e == std::string::npos) e = s.size();
                    v = s.substr(p + 1, e - p - 1);
                    p = e + 1;
                }
            }
            n->attrs.emplace_back(k, unesc(v));
        }
        // content
        for (;;) {
            size_t lt = s.find('<', p);
            if (lt == std::string::npos) {
                n->text += unesc(s.substr(p));
                p = s.size();
                return n;
            }
            n->text += unesc(s.substr(p, lt - p));
            p = lt;
            if (s.compare(p, 2, "</") == 0) {
                size_t e = s.find('>', p);
                p = e == std::string::npos ? s.size() : e + 1;
                return n;
            }
            auto k = parse();
            if (!k) return n;
            n->kids.push_back(std::move(k));
        }
    }
};

std::unique_ptr<Node> parseFile(const std::string& path) {
    std::vector<uint8_t> d;
    if (!readWholeFile(path, d)) return nullptr;
    std::string s(d.begin(), d.end());
    Parser p(s);
    return p.parse();
}

void writeVars(std::string& out, const VarMap& m, const char* indent) {
    for (auto& kv : m) {
        const Variable& v = kv.second;
        if (v.type == Variable::NUL) continue;
        out += indent;
        out += "<var name=\"" + esc(kv.first) + "\" type=\"" + (v.type == Variable::STR ? "str" : "int") +
               "\" value=\"" + esc(v.type == Variable::STR ? v.s : std::to_string(v.i)) + "\" />\n";
    }
}

void readVars(const Node* n, VarMap& m) {
    if (!n) return;
    for (auto& k : n->kids) {
        if (k->tag != "var") continue;
        std::string name = k->attr("name"), type = k->attr("type"), value = k->attr("value");
        if (name.empty()) continue;
        if (type == "str") m[name] = Variable::ofStr(value);
        else if (type == "int") m[name] = Variable::ofInt(atoi(value.c_str()));
    }
}

}  // namespace

std::string nowString() {
    time_t t = time(nullptr);
    struct tm* tm = localtime(&t);
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d %d/%02d/%02d", tm->tm_hour, tm->tm_min, tm->tm_year + 1900, tm->tm_mon + 1,
             tm->tm_mday);
    return buf;
}

bool writeSave(const std::string& path, const SaveState& s) {
    std::string o = "<save>\n";
    o += "  <script><file>script/" + esc(s.file) + "</file><position>" + std::to_string(s.textPos) +
         "</position><line>" + std::to_string(s.line) + "</line></script>\n";
    o += "  <date>" + esc(s.date.empty() ? nowString() : s.date) + "</date>\n";
    o += "  <stamp>" + std::to_string(s.stamp ? s.stamp : (long long)time(nullptr)) + "</stamp>\n";
    o += "  <text>" + esc(s.lastText) + "</text>\n";
    if (s.pinSprites >= 0) o += "  <vn3ds pinsprites=\"" + std::to_string(s.pinSprites) + "\"/>\n";
    o += "  <variables>\n";
    writeVars(o, s.vars, "    ");
    o += "  </variables>\n  <state>\n";
    o += "    <music>" + (s.music.empty() ? std::string() : "sound/" + esc(s.music)) + "</music>\n";
    o += "    <background>" + (s.background.empty() ? std::string() : "background/" + esc(s.background)) +
         "</background>\n";
    o += "    <sprites>\n";
    for (auto& sp : s.sprites)
        o += "      <sprite path=\"foreground/" + esc(sp.path) + "\" x=\"" + std::to_string(sp.x) + "\" y=\"" +
             std::to_string(sp.y) + "\"/>\n";
    o += "    </sprites>\n  </state>\n</save>\n";
    makeDirs(dirName(path));
    return writeWholeFile(path, o.data(), o.size());
}

bool readSave(const std::string& path, SaveState& s) {
    auto root = parseFile(path);
    if (!root || root->tag != "save") return false;
    s = SaveState();
    if (const Node* sc = root->child("script")) {
        s.file = sc->childText("file");
        s.textPos = atoi(sc->childText("position").c_str());
        std::string line = sc->childText("line");
        s.line = line.empty() ? -1 : atoi(line.c_str());
    }
    s.date = root->childText("date");
    s.stamp = atoll(root->childText("stamp").c_str());
    if (const Node* t = root->child("text")) s.lastText = t->text;
    if (const Node* x = root->child("vn3ds")) {
        std::string pin = x->attr("pinsprites");
        if (!pin.empty()) s.pinSprites = atoi(pin.c_str());
    }
    readVars(root->child("variables"), s.vars);
    if (const Node* st = root->child("state")) {
        s.music = st->childText("music");
        s.background = st->childText("background");
        if (const Node* sp = st->child("sprites")) {
            for (auto& k : sp->kids) {
                if (k->tag != "sprite") continue;
                SpriteState x;
                x.path = k->attr("path");
                x.x = atoi(k->attr("x").c_str());
                x.y = atoi(k->attr("y").c_str());
                if (!x.path.empty()) s.sprites.push_back(x);
            }
        }
    }
    return true;
}

bool writeGlobals(const std::string& path, const VarMap& g) {
    std::string o = "<global>\n";
    writeVars(o, g, "  ");
    o += "</global>\n";
    makeDirs(dirName(path));
    return writeWholeFile(path, o.data(), o.size());
}

bool readGlobals(const std::string& path, VarMap& g) {
    auto root = parseFile(path);
    if (!root) return false;
    readVars(root.get(), g);
    return true;
}

}  // namespace vn
