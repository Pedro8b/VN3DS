// TrueType text rendering with stb_truetype into a glyph atlas (GPU_A8).
// Primary font = the novel's default.ttf; glyphs it lacks come from the bundled
// fallback font, so Cyrillic text works even with tiny Latin-only novel fonts.
#pragma once
#include <3ds.h>
#include <citro2d.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "../third_party/stb_truetype.h"

class FontRenderer {
public:
    bool init();
    void shutdown();
    bool setPrimary(std::vector<uint8_t>&& ttf);  // empty = none
    bool loadFallback(const std::string& path);
    void setSize(int px);
    int size() const { return size_; }
    float lineHeight() const { return lineH_; }

    float measure(const std::string& s);
    // Word wrap (spaces) + character wrap for CJK / overlong words.
    std::vector<std::string> wrap(const std::string& s, float width);
    // y is the top of the line. maxChars limits drawn codepoints (typewriter).
    void draw(const std::string& s, float x, float y, u32 color, size_t maxChars = (size_t)-1);
    static size_t countChars(const std::string& s);

private:
    struct Face {
        std::vector<uint8_t> data;
        stbtt_fontinfo info;
        bool ok = false;
    };
    struct Glyph {
        Tex3DS_SubTexture sub;
        float xoff = 0, yoff = 0, adv = 0;
        int w = 0, h = 0;
        int face = 0;
    };
    const Glyph& glyph(uint32_t cp);
    int faceFor(uint32_t cp);
    void resetAtlas();
    void computeMetrics();
    float scaleFor(int face) const;

    Face faces_[2];  // 0 = primary, 1 = fallback
    C3D_Tex atlas_;
    bool atlasOk_ = false;
    int atlasW_ = 1024, atlasH_ = 1024;
    int penX_ = 0, penY_ = 0, rowH_ = 0;
    int size_ = 15;
    float lineH_ = 18, ascent_ = 14;
    bool dirty_ = false;
    std::unordered_map<uint64_t, Glyph> cache_;  // key = size << 32 | codepoint
    std::vector<uint8_t> tmp_;
};
