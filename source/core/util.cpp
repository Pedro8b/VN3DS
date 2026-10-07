#include "util.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

namespace vn {

static void (*g_sink)(LogLevel, const char*) = nullptr;
static LogLevel g_minLevel = LOG_INFO;

void setLogSink(void (*sink)(LogLevel, const char*)) { g_sink = sink; }
void setLogLevel(LogLevel minLevel) { g_minLevel = minLevel; }

void logf(LogLevel lvl, const char* fmt, ...) {
    if (lvl < g_minLevel) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_sink) {
        g_sink(lvl, buf);
    } else {
        static const char* names[] = {"debug", "info", "warn", "error"};
        fprintf(stderr, "[%s] %s\n", names[lvl], buf);
    }
}

std::string toLower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

std::string normPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    std::string out;
    out.reserve(p.size());
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '/' && !out.empty() && out.back() == '/') {
            // keep "sdmc://" style? no: collapse, but preserve "x:/" prefixes as-is
            continue;
        }
        out.push_back(p[i]);
    }
    while (startsWith(out, "./")) out.erase(0, 2);
    return out;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

std::string fileExt(const std::string& p) {
    size_t slash = p.find_last_of('/');
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return toLower(p.substr(dot + 1));
}

std::string stripExt(const std::string& p) {
    size_t slash = p.find_last_of('/');
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return p;
    return p.substr(0, dot);
}

std::string baseName(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string dirName(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDirs(const std::string& p) {
    if (p.empty() || dirExists(p)) return true;
    std::string parent = dirName(p);
    if (!parent.empty() && parent != p && parent.back() != ':') makeDirs(parent);
#ifdef _WIN32
    mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0777);
#endif
    return dirExists(p);
}

std::vector<std::string> listDir(const std::string& p, bool wantDirs, bool wantFiles) {
    std::vector<std::string> out;
    DIR* d = opendir(p.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        bool isDir;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) isDir = true;
        else if (e->d_type == DT_REG) isDir = false;
        else isDir = dirExists(joinPath(p, name));
#else
        isDir = dirExists(joinPath(p, name));
#endif
        if ((isDir && wantDirs) || (!isDir && wantFiles)) out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool readWholeFile(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)n);
    size_t got = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

bool writeWholeFile(const std::string& p, const void* data, size_t len) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    size_t w = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return w == len;
}

uint32_t utf8Next(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else return 0xFFFD;
    for (int k = 0; k < extra; k++) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    return cp;
}

bool Ini::loadFromMemory(const std::string& text) {
    items.clear();
    size_t i = 0;
    // skip UTF-8 BOM
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        i = 3;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(i, e - i));
        i = e + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        items.emplace_back(toLower(trim(line.substr(0, eq))), trim(line.substr(eq + 1)));
    }
    return true;
}

bool Ini::load(const std::string& path) {
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data)) return false;
    return loadFromMemory(std::string(data.begin(), data.end()));
}

std::string Ini::get(const std::string& key, const std::string& def) const {
    std::string k = toLower(key);
    for (auto& kv : items)
        if (kv.first == k) return kv.second;
    return def;
}

int Ini::getInt(const std::string& key, int def) const {
    std::string v = get(key);
    if (v.empty()) return def;
    return atoi(v.c_str());
}

}  // namespace vn
