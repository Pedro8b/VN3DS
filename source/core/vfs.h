// Virtual file system: plain files, zip archives (stored + deflate), Higurashi-Vita
// ".legArchive" bundles, and a resolver that maps VNDS resource names onto whatever
// folder layout a novel happens to use.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "util.h"

namespace vn {

class Stream {
public:
    virtual ~Stream() {}
    virtual size_t read(void* dst, size_t n) = 0;
    virtual bool seek(int64_t off, int whence) = 0;  // SEEK_SET / SEEK_CUR / SEEK_END
    virtual int64_t tell() = 0;
    virtual int64_t size() = 0;
    // Optional direct access for memory-backed streams (nullptr otherwise).
    virtual const uint8_t* data() { return nullptr; }

    bool readAll(std::vector<uint8_t>& out);
};

// A window [base, base+len) of a file on disk. len < 0 means "to end of file".
class FileStream : public Stream {
public:
    static std::unique_ptr<FileStream> open(const std::string& path, int64_t base = 0, int64_t len = -1);
    ~FileStream() override;
    size_t read(void* dst, size_t n) override;
    bool seek(int64_t off, int whence) override;
    int64_t tell() override { return pos_; }
    int64_t size() override { return len_; }

private:
    FileStream() {}
    FILE* f_ = nullptr;
    int64_t base_ = 0, len_ = 0, pos_ = 0;
    std::vector<char> vbuf_;
};

class MemStream : public Stream {
public:
    explicit MemStream(std::vector<uint8_t>&& d) : buf_(std::move(d)) {}
    size_t read(void* dst, size_t n) override;
    bool seek(int64_t off, int whence) override;
    int64_t tell() override { return pos_; }
    int64_t size() override { return (int64_t)buf_.size(); }
    const uint8_t* data() override { return buf_.data(); }

private:
    std::vector<uint8_t> buf_;
    int64_t pos_ = 0;
};

// Read-only archive with a compact case-insensitive index.
class Archive {
public:
    enum Kind { ZIP, LEG };
    static std::shared_ptr<Archive> openZip(const std::string& path, int64_t base = 0, int64_t len = -1);
    static std::shared_ptr<Archive> openLeg(const std::string& path, int64_t base = 0, int64_t len = -1);
    // Absolute file offset of an entry's data (-1 on error). For stored entries this
    // lets nested archives (background.zip inside novel.zip) be opened in place.
    int64_t dataOffset(int idx) const;
    bool isStored(int idx) const { return entries_[idx].method == 0; }

    // Index lookup on a lower-cased, '/'-separated path. Returns -1 if missing.
    int find(const std::string& lowerPath) const;
    // All entries whose path without extension equals lowerStem.
    std::vector<int> findStem(const std::string& lowerStem) const;
    std::unique_ptr<Stream> open(int idx) const;

    size_t count() const { return entries_.size(); }
    std::string name(int idx) const { return std::string(&names_[entries_[idx].nameOff]); }
    uint64_t entrySize(int idx) const { return entries_[idx].size; }
    const std::string& path() const { return path_; }
    Kind kind() const { return kind_; }
    // Remove a leading "dir/" from every name (used when foo.zip stores foo/...).
    void stripPrefix(const std::string& lowerPrefix);
    // Lists entry names (lower-cased) directly under lowerDir ("" = root).
    std::vector<std::string> listTop() const;

private:
    struct Entry {
        uint32_t nameOff;
        uint32_t method;     // 0 = stored, 8 = deflate
        uint64_t hdrOff;     // zip: local header offset; leg: data offset
        uint64_t csize, size;
    };
    void addEntry(const std::string& name, uint32_t method, uint64_t hdrOff, uint64_t csize, uint64_t size);
    void buildIndex();

    Kind kind_ = ZIP;
    std::string path_;
    int64_t base_ = 0;
    std::vector<Entry> entries_;
    std::vector<char> names_;  // NUL-separated lower-cased names
    std::vector<std::pair<uint64_t, uint32_t>> stemIdx_;  // (hash(stem), entry) sorted
};

enum ResCat { RES_SCRIPT = 0, RES_BACKGROUND, RES_FOREGROUND, RES_SOUND, RES_COUNT };

struct NovelEntry {
    std::string path;    // folder or .zip on the SD card
    std::string title;
    std::string layout;  // human readable: "VNDS", "Vita", "zip", ...
    bool isZip = false;
};

enum EngineType { ENGINE_VNDS = 0, ENGINE_HIGURASHI };

class Novel {
public:
    bool open(const std::string& path);
    void close();

    // Resolve and open a resource. 'resolved' (optional) gets a description of
    // what was actually opened, for logs. Returns null when nothing matches.
    std::unique_ptr<Stream> openRes(ResCat cat, const std::string& name, std::string* resolved = nullptr);
    bool exists(ResCat cat, const std::string& name);
    // Open a file relative to the novel root (info.txt, icon.png, default.ttf...).
    std::unique_ptr<Stream> openRoot(const std::string& name);

    const std::string& root() const { return root_; }
    const std::string& title() const { return title_; }
    const std::string& layout() const { return layout_; }
    const std::string& saveDir() const { return saveDir_; }
    int baseWidth() const { return baseW_; }
    int baseHeight() const { return baseH_; }
    bool hasImgIni() const { return hasImgIni_; }
    void setBaseSize(int w, int h) { baseW_ = w; baseH_ = h; }
    int fontSize() const { return fontSize_; }
    std::string describe() const;
    EngineType engine() const { return engine_; }
    const std::string& mainScript() const { return mainScript_; }
    // Script files available (names relative to the script folder).
    std::vector<std::string> listScripts() const;
    bool readRootText(const std::string& name, std::string& out);
    // Higurashi-Vita "CGAlt" art preference.
    bool altArt() const { return altArt_; }

    static void setSaveRoot(const std::string& dir);  // for zip novels
    static NovelEntry probe(const std::string& path);  // empty title if not a novel
    static std::vector<NovelEntry> scan(const std::vector<std::string>& roots);
    // Reads a small file (icon.png, thumbnail.png...) from a novel without opening it fully.
    static bool readRootFile(const NovelEntry& e, const std::string& name, std::vector<uint8_t>& out);

private:
    struct Source {
        std::string dir;                 // plain folder, or
        std::shared_ptr<Archive> arc;    // archive + prefix
        std::string prefix;              // lower-case, ends with '/' or empty
    };
    struct Hit {
        int src = -1;
        int entry = -1;     // archive entry
        std::string path;   // folder file path
    };
    bool lookup(ResCat cat, const std::string& name, Hit& hit);
    bool tryExact(ResCat cat, const std::string& n, Hit& hit);
    bool tryStem(ResCat cat, const std::string& n, Hit& hit);
    std::unique_ptr<Stream> openHit(ResCat cat, const Hit& hit);
    void addDir(ResCat cat, const std::string& dir);
    void addArchive(ResCat cat, std::shared_ptr<Archive> arc, const std::string& prefix);

    std::string root_, title_, layout_, saveDir_;
    std::shared_ptr<Archive> rootArc_;  // zip novels
    std::string rootPrefix_;
    int baseW_ = 256, baseH_ = 192, fontSize_ = 0;
    bool hasImgIni_ = false;
    EngineType engine_ = ENGINE_VNDS;
    std::string mainScript_ = "main.scr";
    bool altArt_ = false;
    void detectEngine(const Ini& override);
    std::vector<Source> src_[RES_COUNT];
    std::unordered_map<std::string, Hit> cache_[RES_COUNT];
};

const std::vector<std::string>& extsFor(ResCat cat);

}  // namespace vn
