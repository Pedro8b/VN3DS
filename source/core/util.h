// Small string / path helpers shared by the core and the 3DS frontend.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vn {

enum LogLevel { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR };
void logf(LogLevel lvl, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
// Optional sink so the frontend can show errors on screen / write a log file.
void setLogSink(void (*sink)(LogLevel, const char*));
void setLogLevel(LogLevel minLevel);

std::string toLower(std::string s);
std::string trim(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
bool iequals(const std::string& a, const std::string& b);
std::vector<std::string> split(const std::string& s, char sep);

// Path helpers (always forward slashes internally).
std::string normPath(std::string p);              // backslashes -> '/', collapse "//", strip "./"
std::string joinPath(const std::string& a, const std::string& b);
std::string fileExt(const std::string& p);        // lowercased, without dot ("" if none)
std::string stripExt(const std::string& p);       // path without extension
std::string baseName(const std::string& p);       // last component
std::string dirName(const std::string& p);        // everything before last '/'

// Filesystem (POSIX-ish; works on 3DS newlib sdmc:/ and on Linux).
bool fileExists(const std::string& p);
bool dirExists(const std::string& p);
bool makeDirs(const std::string& p);
std::vector<std::string> listDir(const std::string& p, bool wantDirs, bool wantFiles);
bool readWholeFile(const std::string& p, std::vector<uint8_t>& out);
bool writeWholeFile(const std::string& p, const void* data, size_t len);

// UTF-8 decoding: returns next codepoint and advances i. Invalid bytes -> U+FFFD.
uint32_t utf8Next(const std::string& s, size_t& i);

// Very small ini reader: key=value per line, '#' or ';' comments.
struct Ini {
    std::vector<std::pair<std::string, std::string>> items;
    bool load(const std::string& path);
    bool loadFromMemory(const std::string& text);
    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def) const;
};

}  // namespace vn
