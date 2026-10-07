#include "vfs.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace vn {

// ---------------------------------------------------------------------------
// Streams

bool Stream::readAll(std::vector<uint8_t>& out) {
    int64_t n = size() - tell();
    if (n < 0) return false;
    out.resize((size_t)n);
    return n == 0 || read(out.data(), (size_t)n) == (size_t)n;
}

std::unique_ptr<FileStream> FileStream::open(const std::string& path, int64_t base, int64_t len) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::unique_ptr<FileStream> s(new FileStream());
    s->f_ = f;
    s->vbuf_.resize(16 * 1024);
    setvbuf(f, s->vbuf_.data(), _IOFBF, s->vbuf_.size());
    if (len < 0) {
        fseek(f, 0, SEEK_END);
        len = (int64_t)ftell(f) - base;
        if (len < 0) len = 0;
    }
    s->base_ = base;
    s->len_ = len;
    fseek(f, (long)base, SEEK_SET);
    return s;
}

FileStream::~FileStream() {
    if (f_) fclose(f_);
}

size_t FileStream::read(void* dst, size_t n) {
    if (pos_ >= len_) return 0;
    if ((int64_t)n > len_ - pos_) n = (size_t)(len_ - pos_);
    size_t got = fread(dst, 1, n, f_);
    pos_ += (int64_t)got;
    return got;
}

bool FileStream::seek(int64_t off, int whence) {
    int64_t np = whence == SEEK_SET ? off : whence == SEEK_CUR ? pos_ + off : len_ + off;
    if (np < 0 || np > len_) return false;
    if (np == pos_) return true;
    if (fseek(f_, (long)(base_ + np), SEEK_SET) != 0) return false;
    pos_ = np;
    return true;
}

size_t MemStream::read(void* dst, size_t n) {
    int64_t left = (int64_t)buf_.size() - pos_;
    if (left <= 0) return 0;
    if ((int64_t)n > left) n = (size_t)left;
    memcpy(dst, buf_.data() + pos_, n);
    pos_ += (int64_t)n;
    return n;
}

bool MemStream::seek(int64_t off, int whence) {
    int64_t np = whence == SEEK_SET ? off : whence == SEEK_CUR ? pos_ + off : (int64_t)buf_.size() + off;
    if (np < 0 || np > (int64_t)buf_.size()) return false;
    pos_ = np;
    return true;
}

// ---------------------------------------------------------------------------
// Archives

static uint64_t fnv1a(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static size_t stemLen(const char* s) {
    size_t n = strlen(s);
    for (size_t i = n; i > 0; i--) {
        if (s[i - 1] == '/') break;
        if (s[i - 1] == '.') return i - 1;
    }
    return n;
}

static uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

void Archive::addEntry(const std::string& name, uint32_t method, uint64_t hdrOff, uint64_t csize, uint64_t size) {
    Entry e;
    e.nameOff = (uint32_t)names_.size();
    e.method = method;
    e.hdrOff = hdrOff;
    e.csize = csize;
    e.size = size;
    std::string ln = toLower(normPath(name));
    while (!ln.empty() && ln[0] == '/') ln.erase(0, 1);
    names_.insert(names_.end(), ln.begin(), ln.end());
    names_.push_back('\0');
    entries_.push_back(e);
}

void Archive::buildIndex() {
    stemIdx_.clear();
    stemIdx_.reserve(entries_.size());
    for (uint32_t i = 0; i < entries_.size(); i++) {
        const char* n = &names_[entries_[i].nameOff];
        stemIdx_.emplace_back(fnv1a(n, stemLen(n)), i);
    }
    std::sort(stemIdx_.begin(), stemIdx_.end());
}

void Archive::stripPrefix(const std::string& lowerPrefix) {
    std::vector<char> old;
    old.swap(names_);
    for (auto& e : entries_) {
        const char* n = &old[e.nameOff];
        if (strncmp(n, lowerPrefix.c_str(), lowerPrefix.size()) == 0) n += lowerPrefix.size();
        e.nameOff = (uint32_t)names_.size();
        names_.insert(names_.end(), n, n + strlen(n) + 1);
    }
    buildIndex();
}

std::vector<int> Archive::findStem(const std::string& lowerStem) const {
    std::vector<int> out;
    uint64_t h = fnv1a(lowerStem.c_str(), lowerStem.size());
    auto it = std::lower_bound(stemIdx_.begin(), stemIdx_.end(), std::make_pair(h, (uint32_t)0));
    for (; it != stemIdx_.end() && it->first == h; ++it) {
        const char* n = &names_[entries_[it->second].nameOff];
        size_t sl = stemLen(n);
        if (sl == lowerStem.size() && memcmp(n, lowerStem.data(), sl) == 0) out.push_back((int)it->second);
    }
    return out;
}

int Archive::find(const std::string& lowerPath) const {
    size_t sl = stemLen(lowerPath.c_str());
    for (int i : findStem(lowerPath.substr(0, sl)))
        if (lowerPath == &names_[entries_[i].nameOff]) return i;
    return -1;
}

std::vector<std::string> Archive::listTop() const {
    std::vector<std::string> out;
    for (auto& e : entries_) {
        std::string n = &names_[e.nameOff];
        size_t s = n.find('/');
        std::string top = s == std::string::npos ? n : n.substr(0, s + 1);
        if (std::find(out.begin(), out.end(), top) == out.end()) out.push_back(top);
    }
    return out;
}

std::shared_ptr<Archive> Archive::openZip(const std::string& path, int64_t base, int64_t len) {
    auto fs = FileStream::open(path, base, len);
    if (!fs) return nullptr;
    int64_t fsize = fs->size();
    if (fsize < 22) return nullptr;
    // Locate the end-of-central-directory record.
    int64_t tail = std::min<int64_t>(fsize, 65557);
    std::vector<uint8_t> buf((size_t)tail);
    fs->seek(fsize - tail, SEEK_SET);
    if (fs->read(buf.data(), buf.size()) != buf.size()) return nullptr;
    int64_t eocd = -1;
    for (int64_t i = tail - 22; i >= 0; i--) {
        if (rd32(&buf[(size_t)i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        logf(LOG_WARN, "zip: no central directory in %s", path.c_str());
        return nullptr;
    }
    const uint8_t* e = &buf[(size_t)eocd];
    uint32_t count = rd16(e + 10);
    uint32_t cdSize = rd32(e + 12);
    uint32_t cdOff = rd32(e + 16);
    // Archives that were cut out of a bigger file (or have a prefix) - adjust.
    int64_t eocdAbs = fsize - tail + eocd;
    int64_t shift = eocdAbs - ((int64_t)cdOff + cdSize);
    if (shift < 0) shift = 0;

    std::vector<uint8_t> cd(cdSize);
    fs->seek(cdOff + shift, SEEK_SET);
    if (fs->read(cd.data(), cdSize) != cdSize) return nullptr;

    std::shared_ptr<Archive> a(new Archive());
    a->kind_ = ZIP;
    a->path_ = path;
    a->base_ = base + shift;
    a->entries_.reserve(count);
    size_t p = 0;
    std::string name;
    while (p + 46 <= cd.size() && rd32(&cd[p]) == 0x02014b50) {
        uint32_t method = rd16(&cd[p + 10]);
        uint32_t csize = rd32(&cd[p + 20]);
        uint32_t usize = rd32(&cd[p + 24]);
        uint32_t nlen = rd16(&cd[p + 28]);
        uint32_t xlen = rd16(&cd[p + 30]);
        uint32_t clen = rd16(&cd[p + 32]);
        uint32_t loff = rd32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) break;
        name.assign((const char*)&cd[p + 46], nlen);
        if (!name.empty() && name.back() != '/' && name.back() != '\\') {
            a->addEntry(name, method, loff, csize, usize);
        }
        p += 46 + nlen + xlen + clen;
    }
    a->buildIndex();
    logf(LOG_INFO, "zip: %s (%u files)", path.c_str(), (unsigned)a->entries_.size());
    return a;
}

std::shared_ptr<Archive> Archive::openLeg(const std::string& path, int64_t base, int64_t len) {
    auto fs = FileStream::open(path, base, len);
    if (!fs) return nullptr;
    uint8_t hdr[8];
    if (fs->read(hdr, 8) != 8 || memcmp(hdr, "LEGARCH", 7) != 0) return nullptr;
    int64_t fsize = fs->size();
    uint8_t foot[8];
    fs->seek(fsize - 8, SEEK_SET);
    if (fs->read(foot, 8) != 8) return nullptr;
    uint64_t tab = rd64(foot);
    if (tab < 8 || (int64_t)tab >= fsize - 8) return nullptr;
    std::vector<uint8_t> t((size_t)(fsize - 8 - (int64_t)tab));
    fs->seek((int64_t)tab, SEEK_SET);
    if (fs->read(t.data(), t.size()) != t.size()) return nullptr;
    size_t p = 0;
    if (t.size() >= 10 && memcmp(t.data(), "LEGARCHTBL", 10) == 0) p = 10;
    if (p + 4 > t.size()) return nullptr;
    uint32_t count = rd32(&t[p]);
    p += 4;
    std::shared_ptr<Archive> a(new Archive());
    a->kind_ = LEG;
    a->path_ = path;
    a->base_ = base;
    a->entries_.reserve(count);
    for (uint32_t i = 0; i < count && p < t.size(); i++) {
        size_t s = p;
        while (p < t.size() && t[p]) p++;
        if (p + 13 > t.size()) break;
        std::string name((const char*)&t[s], p - s);
        p++;
        uint64_t off = rd64(&t[p]);
        uint32_t size = rd32(&t[p + 8]);
        p += 12;
        if (off + size <= (uint64_t)fsize) a->addEntry(name, 0, off, size, size);
    }
    a->buildIndex();
    logf(LOG_INFO, "legArchive: %s (%u files)", path.c_str(), (unsigned)a->entries_.size());
    return a;
}

int64_t Archive::dataOffset(int idx) const {
    if (idx < 0 || idx >= (int)entries_.size()) return -1;
    const Entry& e = entries_[idx];
    if (kind_ == LEG) return base_ + (int64_t)e.hdrOff;
    // zip: read local header to find the data offset
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) return -1;
    uint8_t lh[30];
    fseek(f, (long)(base_ + (int64_t)e.hdrOff), SEEK_SET);
    size_t got = fread(lh, 1, 30, f);
    fclose(f);
    if (got != 30 || rd32(lh) != 0x04034b50) {
        logf(LOG_WARN, "zip: bad local header for %s", name(idx).c_str());
        return -1;
    }
    return base_ + (int64_t)e.hdrOff + 30 + rd16(lh + 26) + rd16(lh + 28);
}

std::unique_ptr<Stream> Archive::open(int idx) const {
    int64_t dataOff = dataOffset(idx);
    if (dataOff < 0) return nullptr;
    const Entry& e = entries_[idx];
    if (e.method == 0) return FileStream::open(path_, dataOff, (int64_t)e.size);
    if (e.method != 8) {
        logf(LOG_WARN, "zip: unsupported compression %u for %s", (unsigned)e.method, name(idx).c_str());
        return nullptr;
    }
    auto src = FileStream::open(path_, dataOff, (int64_t)e.csize);
    if (!src) return nullptr;
    std::vector<uint8_t> comp((size_t)e.csize), out((size_t)e.size);
    if (src->read(comp.data(), comp.size()) != comp.size()) return nullptr;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return nullptr;
    zs.next_in = comp.data();
    zs.avail_in = (uInt)comp.size();
    zs.next_out = out.data();
    zs.avail_out = (uInt)out.size();
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END) {
        logf(LOG_WARN, "zip: inflate failed for %s", name(idx).c_str());
        return nullptr;
    }
    return std::unique_ptr<Stream>(new MemStream(std::move(out)));
}

// ---------------------------------------------------------------------------
// Novel resolver

const std::vector<std::string>& extsFor(ResCat cat) {
    static const std::vector<std::string> img = {"png", "jpg", "jpeg", "bmp", "gif", "tga", "psd", "webp"};
    static const std::vector<std::string> snd = {"ogg", "mp3", "wv", "aac", "m4a", "wav", "flac", "opus", "adts"};
    static const std::vector<std::string> scr = {"scr", "txt"};
    switch (cat) {
        case RES_SCRIPT: return scr;
        case RES_SOUND: return snd;
        default: return img;
    }
}

static std::string g_saveRoot = "saves";
void Novel::setSaveRoot(const std::string& dir) { g_saveRoot = dir; }

// Case-insensitive child lookup inside a real folder; returns "" when missing.
static std::string findChild(const std::vector<std::string>& listing, const std::string& name) {
    for (auto& n : listing)
        if (iequals(n, name)) return n;
    return "";
}

void Novel::addDir(ResCat cat, const std::string& dir) {
    Source s;
    s.dir = dir;
    src_[cat].push_back(s);
}

void Novel::addArchive(ResCat cat, std::shared_ptr<Archive> arc, const std::string& prefix) {
    if (!arc) return;
    Source s;
    s.arc = arc;
    s.prefix = prefix;
    src_[cat].push_back(s);
}

void Novel::close() {
    for (int c = 0; c < RES_COUNT; c++) {
        src_[c].clear();
        cache_[c].clear();
    }
    rootArc_.reset();
    root_.clear();
}

// Category -> folder names that different VNDS ports use (priority order).
static const char* const kDirs[RES_COUNT][6] = {
    {"script", "scripts", nullptr},
    {"background", "cg", "bg", "cgalt", "foreground", nullptr},
    {"foreground", "cgalt", "fg", "cg", "background", nullptr},
    {"sound", "se", "bgm", "voice", "music", nullptr},
};
static const char* const kZips[RES_COUNT][3] = {
    {"script", nullptr},
    {"background", "foreground", nullptr},
    {"foreground", "background", nullptr},
    {"sound", "music", nullptr},
};

static std::shared_ptr<Archive> openCategoryZip(const std::string& path, int64_t base, int64_t len,
                                                const std::string& stem) {
    auto a = Archive::openZip(path, base, len);
    if (!a) return a;
    // background.zip usually stores "background/xxx.jpg": strip that prefix if all entries have it.
    std::string pre = toLower(stem) + "/";
    size_t with = 0;
    for (size_t i = 0; i < a->count(); i++)
        if (startsWith(a->name((int)i), pre)) with++;
    if (with > 0 && with == a->count()) a->stripPrefix(pre);
    return a;
}

bool Novel::open(const std::string& pathIn) {
    close();
    std::string path = normPath(pathIn);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    std::vector<std::string> layoutBits;

    if (fileExists(path)) {
        // Whole novel packed in a single zip.
        rootArc_ = Archive::openZip(path);
        if (!rootArc_) return false;
        root_ = path;
        rootPrefix_.clear();
        auto top = rootArc_->listTop();
        bool rootHasNovel = false;
        for (auto& t : top)
            if (t == "info.txt" || t == "script/" || t == "scripts/" || t == "script.zip" || t == "isvnds" ||
                t == "includedpreset.txt" || t == "vn3ds.ini")
                rootHasNovel = true;
        if (!rootHasNovel) {
            for (auto& t : top) {
                if (!t.empty() && t.back() == '/') {
                    rootPrefix_ = t;
                    break;
                }
            }
        }
        if (rootArc_->find(rootPrefix_ + "streamingassets/info.txt") >= 0 ||
            rootArc_->findStem(rootPrefix_ + "streamingassets/isvnds").size())
            rootPrefix_ += "streamingassets/";
        for (int c = 0; c < RES_COUNT; c++) {
            for (int k = 0; kDirs[c][k]; k++) {
                std::string pre = rootPrefix_ + kDirs[c][k] + "/";
                bool any = false;
                // cheap check: is there any entry under that prefix?
                for (size_t i = 0; i < rootArc_->count() && !any; i++)
                    if (startsWith(rootArc_->name((int)i), pre)) any = true;
                if (any) addArchive((ResCat)c, rootArc_, pre);
            }
            for (int k = 0; kZips[c][k]; k++) {
                int idx = rootArc_->find(rootPrefix_ + kZips[c][k] + ".zip");
                if (idx < 0) continue;
                // nested zips work in place when stored uncompressed (the usual VNDS case)
                if (!rootArc_->isStored(idx)) {
                    logf(LOG_WARN, "%s.zip inside the novel zip is compressed; repack it as 'store'", kZips[c][k]);
                    continue;
                }
                int64_t off = rootArc_->dataOffset(idx);
                if (off >= 0)
                    addArchive((ResCat)c, openCategoryZip(root_, off, (int64_t)rootArc_->entrySize(idx), kZips[c][k]), "");
            }
        }
        for (size_t i = 0; i < rootArc_->count(); i++) {
            std::string n = rootArc_->name((int)i);
            if (startsWith(n, rootPrefix_) && endsWith(n, ".legarchive") && rootArc_->isStored((int)i)) {
                int64_t off = rootArc_->dataOffset((int)i);
                if (off >= 0) addArchive(RES_SOUND, Archive::openLeg(root_, off, (int64_t)rootArc_->entrySize((int)i)), "");
            }
        }
        layoutBits.push_back("zip");
        saveDir_ = joinPath(g_saveRoot, stripExt(baseName(path)));
    } else if (dirExists(path)) {
        root_ = path;
        auto listing = listDir(root_, true, true);
        std::string sa = findChild(listing, "StreamingAssets");
        if (!sa.empty() && dirExists(joinPath(root_, sa))) {
            root_ = joinPath(root_, sa);
            listing = listDir(root_, true, true);
            layoutBits.push_back("StreamingAssets");
        }
        bool vita = !findChild(listing, "isvnds").empty();
        for (int c = 0; c < RES_COUNT; c++) {
            // Archives first: their in-memory index is much cheaper than probing the SD card.
            for (int k = 0; kZips[c][k]; k++) {
                std::string z = findChild(listing, std::string(kZips[c][k]) + ".zip");
                if (z.empty()) continue;
                // reuse an already opened archive (foreground.zip may serve two categories)
                std::shared_ptr<Archive> arc;
                for (int c2 = 0; c2 < RES_COUNT && !arc; c2++)
                    for (auto& s : src_[c2])
                        if (s.arc && s.arc->path() == joinPath(root_, z)) arc = s.arc;
                if (!arc) arc = openCategoryZip(joinPath(root_, z), 0, -1, kZips[c][k]);
                if (arc) {
                    addArchive((ResCat)c, arc, "");
                    if (std::find(layoutBits.begin(), layoutBits.end(), z) == layoutBits.end()) layoutBits.push_back(z);
                }
            }
            if (c == RES_SOUND) {
                for (auto& n : listing) {
                    if (endsWith(toLower(n), ".legarchive")) {
                        auto arc = Archive::openLeg(joinPath(root_, n));
                        if (arc) {
                            addArchive(RES_SOUND, arc, "");
                            layoutBits.push_back(n);
                            vita = true;
                        }
                    }
                }
            }
            for (int k = 0; kDirs[c][k]; k++) {
                std::string d = findChild(listing, kDirs[c][k]);
                if (d.empty() || !dirExists(joinPath(root_, d))) continue;
                addDir((ResCat)c, joinPath(root_, d));
                if (d == "Scripts" || d == "CG" || d == "CGAlt" || d == "SE") vita = true;
            }
        }
        if (vita) layoutBits.insert(layoutBits.begin(), "Vita");
        saveDir_ = joinPath(root_, "save");
    } else {
        logf(LOG_ERROR, "novel not found: %s", path.c_str());
        return false;
    }

    // Optional per-novel override file: tells VN3DS how the novel is laid out.
    Ini over;
    {
        std::string txt;
        if (readRootText("vn3ds.ini", txt)) {
            over.loadFromMemory(txt);
            layoutBits.push_back("vn3ds.ini");
        }
    }
    static const char* const dirKeys[RES_COUNT] = {"script_dir", "background_dir", "foreground_dir", "sound_dir"};
    for (int c = 0; c < RES_COUNT; c++) {
        auto names = split(over.get(dirKeys[c]), ',');
        for (auto it = names.rbegin(); it != names.rend(); ++it) {
            std::string n = trim(*it);
            if (n.empty()) continue;
            Source s;
            if (rootArc_) {
                s.arc = rootArc_;
                s.prefix = rootPrefix_ + toLower(normPath(n)) + "/";
            } else {
                s.dir = joinPath(root_, normPath(n));
                if (!dirExists(s.dir)) {
                    logf(LOG_WARN, "vn3ds.ini: folder not found: %s", s.dir.c_str());
                    continue;
                }
            }
            src_[c].insert(src_[c].begin(), s);
        }
    }
    detectEngine(over);
    if (engine_ == ENGINE_HIGURASHI) layoutBits.insert(layoutBits.begin(), "Higurashi");

    // info.txt / img.ini
    title_ = baseName(path);
    if (auto s = openRoot("info.txt")) {
        std::vector<uint8_t> d;
        s->readAll(d);
        Ini ini;
        ini.loadFromMemory(std::string(d.begin(), d.end()));
        std::string t = ini.get("title");
        if (!t.empty()) title_ = t;
        fontSize_ = ini.getInt("fontsize", 0);
    }
    hasImgIni_ = false;
    baseW_ = 256;
    baseH_ = 192;
    if (auto s = openRoot("img.ini")) {
        std::vector<uint8_t> d;
        s->readAll(d);
        Ini ini;
        ini.loadFromMemory(std::string(d.begin(), d.end()));
        int w = ini.getInt("width", 0), h = ini.getInt("height", 0);
        if (w > 0 && h > 0) {
            baseW_ = w;
            baseH_ = h;
            hasImgIni_ = true;
        }
    }
    if (!over.get("title").empty()) title_ = over.get("title");
    if (over.getInt("width", 0) > 0 && over.getInt("height", 0) > 0) {
        baseW_ = over.getInt("width", 0);
        baseH_ = over.getInt("height", 0);
        hasImgIni_ = true;
    }
    layout_.clear();
    for (auto& b : layoutBits) layout_ += (layout_.empty() ? "" : ", ") + b;
    if (layout_.empty()) layout_ = "folders";
    logf(LOG_INFO, "novel '%s' [%s] base %dx%d", title_.c_str(), layout_.c_str(), baseW_, baseH_);
    return true;
}

bool Novel::readRootText(const std::string& name, std::string& out) {
    auto s = openRoot(name);
    if (!s) return false;
    std::vector<uint8_t> d;
    if (!s->readAll(d)) return false;
    size_t start = (d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) ? 3 : 0;
    out.assign((const char*)d.data() + start, d.size() - start);
    return true;
}

std::vector<std::string> Novel::listScripts() const {
    std::vector<std::string> out;
    for (auto& s : src_[RES_SCRIPT]) {
        if (s.arc) {
            for (size_t i = 0; i < s.arc->count(); i++) {
                std::string n = s.arc->name((int)i);
                if (startsWith(n, s.prefix)) {
                    std::string rel = n.substr(s.prefix.size());
                    if (!rel.empty() && rel.find('/') == std::string::npos) out.push_back(rel);
                }
            }
        } else {
            for (auto& n : listDir(s.dir, false, true)) out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Novel::detectEngine(const Ini& over) {
    std::string e = toLower(over.get("engine"));
    std::string mainOverride = over.get("main");
    altArt_ = over.getInt("alt_art", 0) != 0;
    auto scripts = listScripts();
    size_t scr = 0, txt = 0;
    bool hasMain = false;
    for (auto& s : scripts) {
        std::string ext = fileExt(s);
        if (ext == "scr") scr++;
        if (ext == "txt") txt++;
        if (iequals(s, "main.scr")) hasMain = true;
    }
    std::string preset;
    bool hasPreset = const_cast<Novel*>(this)->readRootText("includedPreset.txt", preset);
    if (e == "higurashi" || e == "lua") engine_ = ENGINE_HIGURASHI;
    else if (e == "vnds") engine_ = ENGINE_VNDS;
    else if (hasMain) engine_ = ENGINE_VNDS;
    else if (hasPreset || (txt > 0 && scr == 0)) engine_ = ENGINE_HIGURASHI;
    else engine_ = ENGINE_VNDS;

    if (!mainOverride.empty()) {
        mainScript_ = mainOverride;
    } else if (engine_ == ENGINE_VNDS) {
        mainScript_ = "main.scr";
        if (!hasMain) {
            // No main.scr: start from the first .scr we have.
            for (auto& s : scripts)
                if (fileExt(s) == "scr") {
                    mainScript_ = s;
                    break;
                }
            logf(LOG_WARN, "no main.scr, starting from %s", mainScript_.c_str());
        }
    } else {
        mainScript_.clear();  // Higurashi: chapter list comes from the preset
    }
    if (engine_ == ENGINE_HIGURASHI) {
        // Prefer CG over CGAlt (Higurashi-Vita's default art) unless alt_art=1.
        auto& fg = src_[RES_FOREGROUND];
        auto nameOf = [](const Source& s) {
            std::string p = s.arc ? s.prefix : s.dir;
            while (!p.empty() && p.back() == '/') p.pop_back();
            return toLower(baseName(p));
        };
        int cg = -1, alt = -1;
        for (int i = 0; i < (int)fg.size(); i++) {
            if (nameOf(fg[i]) == "cg" && cg < 0) cg = i;
            if (nameOf(fg[i]) == "cgalt" && alt < 0) alt = i;
        }
        if (cg >= 0 && alt >= 0 && ((cg > alt) != altArt_)) std::swap(fg[cg], fg[alt]);
    }
}

std::string Novel::describe() const {
    static const char* names[] = {"script", "background", "foreground", "sound"};
    std::string out;
    for (int c = 0; c < RES_COUNT; c++) {
        out += names[c];
        out += ":";
        for (auto& s : src_[c]) out += " " + (s.arc ? s.arc->path() + "!" + s.prefix : s.dir);
        out += "\n";
    }
    return out;
}

std::unique_ptr<Stream> Novel::openRoot(const std::string& name) {
    if (rootArc_) {
        int idx = rootArc_->find(rootPrefix_ + toLower(name));
        return idx >= 0 ? rootArc_->open(idx) : nullptr;
    }
    std::string p = joinPath(root_, name);
    if (fileExists(p)) return FileStream::open(p);
    std::string c = findChild(listDir(root_, false, true), name);
    if (!c.empty()) return FileStream::open(joinPath(root_, c));
    return nullptr;
}

bool Novel::tryExact(ResCat cat, const std::string& n, Hit& hit) {
    std::string ln = toLower(n);
    for (size_t i = 0; i < src_[cat].size(); i++) {
        Source& s = src_[cat][i];
        if (s.arc) {
            int idx = s.arc->find(s.prefix + ln);
            if (idx >= 0) {
                hit.src = (int)i;
                hit.entry = idx;
                return true;
            }
        } else {
            std::string p = joinPath(s.dir, n);
            if (fileExists(p) || (ln != n && fileExists(p = joinPath(s.dir, ln)))) {
                hit.src = (int)i;
                hit.path = p;
                return true;
            }
        }
    }
    return false;
}

bool Novel::tryStem(ResCat cat, const std::string& n, Hit& hit) {
    std::string stem = stripExt(n);
    std::string lstem = toLower(stem);
    const auto& exts = extsFor(cat);
    for (size_t i = 0; i < src_[cat].size(); i++) {
        Source& s = src_[cat][i];
        if (s.arc) {
            for (int idx : s.arc->findStem(s.prefix + lstem)) {
                std::string e = fileExt(s.arc->name(idx));
                if (std::find(exts.begin(), exts.end(), e) != exts.end()) {
                    hit.src = (int)i;
                    hit.entry = idx;
                    return true;
                }
            }
        } else {
            for (auto& e : exts) {
                std::string p = joinPath(s.dir, stem + "." + e);
                if (fileExists(p) || (lstem != stem && fileExists(p = joinPath(s.dir, lstem + "." + e)))) {
                    hit.src = (int)i;
                    hit.path = p;
                    return true;
                }
            }
        }
    }
    return false;
}

bool Novel::lookup(ResCat cat, const std::string& name, Hit& hit) {
    std::string n = normPath(trim(name));
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    if (n.empty() || n == "~") return false;
    std::string key = toLower(n);
    auto it = cache_[cat].find(key);
    if (it != cache_[cat].end()) {
        hit = it->second;
        return hit.src >= 0;
    }
    std::vector<std::string> cands = {n};
    size_t slash = n.find('/');
    if (slash != std::string::npos) {
        cands.push_back(n.substr(slash + 1));  // "sound/foo.ogg" or "music/foo.mp3" -> drop first dir
        std::string b = baseName(n);
        if (b != cands.back()) cands.push_back(b);
    }
    if (cat == RES_SOUND) {
        // Vita ports keep voices/music in subfolders the scripts don't always name
        // ("26/sys020.ogg" lives at voice/26/sys020.ogg).
        for (const char* pre : {"voice/", "se/", "bgm/", "music/", "sound/"})
            if (!startsWith(toLower(n), pre)) cands.push_back(pre + n);
    }
    bool ok = false;
    for (auto& c : cands)
        if ((ok = tryExact(cat, c, hit))) break;
    if (!ok)
        for (auto& c : cands)
            if ((ok = tryStem(cat, c, hit))) break;
    if (!ok) hit = Hit();
    cache_[cat][key] = hit;
    return ok;
}

std::unique_ptr<Stream> Novel::openHit(ResCat cat, const Hit& hit) {
    const Source& s = src_[cat][hit.src];
    if (s.arc) return s.arc->open(hit.entry);
    return FileStream::open(hit.path);
}

std::unique_ptr<Stream> Novel::openRes(ResCat cat, const std::string& name, std::string* resolved) {
    Hit hit;
    if (!lookup(cat, name, hit)) {
        logf(LOG_WARN, "missing resource: %s", name.c_str());
        return nullptr;
    }
    if (resolved) {
        const Source& s = src_[cat][hit.src];
        *resolved = s.arc ? s.arc->path() + "!" + s.arc->name(hit.entry) : hit.path;
    }
    return openHit(cat, hit);
}

bool Novel::exists(ResCat cat, const std::string& name) {
    Hit hit;
    return lookup(cat, name, hit);
}

// ---------------------------------------------------------------------------
// Library scan

NovelEntry Novel::probe(const std::string& pathIn) {
    NovelEntry e;
    std::string path = normPath(pathIn);
    std::vector<uint8_t> info;
    if (dirExists(path)) {
        auto listing = listDir(path, true, true);
        std::string root = path;
        std::string sa = findChild(listing, "StreamingAssets");
        if (!sa.empty()) {
            root = joinPath(path, sa);
            listing = listDir(root, true, true);
        }
        bool isNovel = false;
        for (const char* marker : {"info.txt", "isvnds", "script", "Scripts", "script.zip", "includedPreset.txt", "vn3ds.ini"})
            if (!findChild(listing, marker).empty()) isNovel = true;
        if (!isNovel) return e;
        std::string inf = findChild(listing, "info.txt");
        if (!inf.empty()) readWholeFile(joinPath(root, inf), info);
        bool vita = !findChild(listing, "isvnds").empty() || !findChild(listing, "Scripts").empty();
        bool higu = !findChild(listing, "includedPreset.txt").empty();
        std::string sd = findChild(listing, "Scripts");
        if (sd.empty()) sd = findChild(listing, "script");
        if (!higu && !sd.empty()) {
            bool scr = false, txt = false;
            for (auto& f : listDir(joinPath(root, sd), false, true)) {
                if (fileExt(f) == "scr") scr = true;
                if (fileExt(f) == "txt") txt = true;
            }
            higu = txt && !scr;
        }
        e.layout = higu ? "Higurashi" : vita ? "Vita" : (!findChild(listing, "script.zip").empty() ? "VNDS zip" : "VNDS");
        std::string ov = findChild(listing, "vn3ds.ini");
        if (!ov.empty()) {
            Ini o;
            if (o.load(joinPath(root, ov)) && !o.get("engine").empty()) e.layout = o.get("engine");
        }
    } else if (fileExists(path) && fileExt(path) == "zip") {
        auto a = Archive::openZip(path);
        if (!a) return e;
        std::string prefix;
        bool found = false;
        for (auto& t : a->listTop())
            if (t == "info.txt" || t == "script/" || t == "scripts/" || t == "isvnds" || t == "includedpreset.txt" || t == "vn3ds.ini") found = true;
        if (!found) {
            for (auto& t : a->listTop()) {
                if (t.back() == '/' && (a->find(t + "info.txt") >= 0 || a->find(t + "script/main.scr") >= 0)) {
                    prefix = t;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return e;
        int idx = a->find(prefix + "info.txt");
        if (idx >= 0)
            if (auto s = a->open(idx)) s->readAll(info);
        e.isZip = true;
        e.layout = "zip";
    } else {
        return e;
    }
    e.path = path;
    e.title = stripExt(baseName(path));
    if (!info.empty()) {
        Ini ini;
        ini.loadFromMemory(std::string(info.begin(), info.end()));
        std::string t = ini.get("title");
        if (!t.empty()) e.title = t;
    }
    return e;
}

bool Novel::readRootFile(const NovelEntry& e, const std::string& name, std::vector<uint8_t>& out) {
    if (!e.isZip) {
        std::string root = e.path;
        auto listing = listDir(root, true, true);
        std::string sa = findChild(listing, "StreamingAssets");
        if (!sa.empty()) {
            root = joinPath(root, sa);
            listing = listDir(root, true, true);
        }
        std::string c = findChild(listing, name);
        return !c.empty() && readWholeFile(joinPath(root, c), out);
    }
    auto a = Archive::openZip(e.path);
    if (!a) return false;
    std::string ln = toLower(name);
    int idx = a->find(ln);
    if (idx < 0)
        for (auto& t : a->listTop())
            if (t.back() == '/' && (idx = a->find(t + ln)) >= 0) break;
    if (idx < 0) return false;
    auto s = a->open(idx);
    return s && s->readAll(out);
}

std::vector<NovelEntry> Novel::scan(const std::vector<std::string>& roots) {
    std::vector<NovelEntry> out;
    for (auto& r : roots) {
        if (!dirExists(r)) continue;
        for (auto& n : listDir(r, true, true)) {
            NovelEntry e = probe(joinPath(r, n));
            if (e.title.empty()) continue;
            bool dup = false;
            for (auto& o : out)
                if (o.path == e.path) dup = true;
            if (!dup) out.push_back(e);
        }
    }
    // Same title twice (e.g. a Vita and a DS copy): show the folder name too.
    std::vector<int> same(out.size(), 0);
    for (size_t i = 0; i < out.size(); i++)
        for (auto& b : out)
            if (toLower(out[i].title) == toLower(b.title)) same[i]++;
    for (size_t i = 0; i < out.size(); i++)
        if (same[i] > 1) out[i].title += " (" + stripExt(baseName(out[i].path)) + ")";
    std::sort(out.begin(), out.end(), [](const NovelEntry& a, const NovelEntry& b) {
        return toLower(a.title) < toLower(b.title);
    });
    return out;
}

}  // namespace vn
