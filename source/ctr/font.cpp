#include "font.h"

#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#include "../core/util.h"
#include "gfx.h"

bool FontRenderer::init() {
    atlasOk_ = C3D_TexInit(&atlas_, (u16)atlasW_, (u16)atlasH_, GPU_A8);
    if (!atlasOk_) return false;
    C3D_TexSetFilter(&atlas_, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&atlas_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    resetAtlas();
    return true;
}

void FontRenderer::shutdown() {
    if (atlasOk_) C3D_TexDelete(&atlas_);
    atlasOk_ = false;
    cache_.clear();
}

void FontRenderer::resetAtlas() {
    memset(atlas_.data, 0, atlas_.size);
    penX_ = penY_ = rowH_ = 0;
    cache_.clear();
    dirty_ = true;
}

bool FontRenderer::setPrimary(std::vector<uint8_t>&& ttf) {
    faces_[0] = Face();
    if (!ttf.empty()) {
        faces_[0].data = std::move(ttf);
        int off = stbtt_GetFontOffsetForIndex(faces_[0].data.data(), 0);
        faces_[0].ok = off >= 0 && stbtt_InitFont(&faces_[0].info, faces_[0].data.data(), off);
        if (!faces_[0].ok) {
            vn::logf(vn::LOG_WARN, "novel font could not be parsed, using the built-in one");
            faces_[0].data.clear();
        }
    }
    resetAtlas();
    computeMetrics();
    return faces_[0].ok;
}

bool FontRenderer::loadFallback(const std::string& path) {
    faces_[1] = Face();
    if (!vn::readWholeFile(path, faces_[1].data)) return false;
    faces_[1].ok = stbtt_InitFont(&faces_[1].info, faces_[1].data.data(), 0) != 0;
    resetAtlas();
    computeMetrics();
    return faces_[1].ok;
}

float FontRenderer::scaleFor(int face) const { return stbtt_ScaleForPixelHeight(&faces_[face].info, (float)size_); }

void FontRenderer::computeMetrics() {
    int f = faces_[0].ok ? 0 : 1;
    if (!faces_[f].ok) return;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&faces_[f].info, &asc, &desc, &gap);
    float sc = scaleFor(f);
    ascent_ = std::ceil(asc * sc);
    lineH_ = std::ceil((asc - desc) * sc) + 2.0f;
}

void FontRenderer::setSize(int px) {
    if (px == size_) return;
    size_ = px;
    computeMetrics();
}

int FontRenderer::faceFor(uint32_t cp) {
    if (faces_[0].ok && stbtt_FindGlyphIndex(&faces_[0].info, (int)cp)) return 0;
    if (faces_[1].ok && stbtt_FindGlyphIndex(&faces_[1].info, (int)cp)) return 1;
    return faces_[0].ok ? 0 : 1;
}

const FontRenderer::Glyph& FontRenderer::glyph(uint32_t cp) {
    uint64_t key = ((uint64_t)size_ << 32) | cp;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Glyph g;
    g.face = faceFor(cp);
    Face& f = faces_[g.face];
    if (f.ok) {
        float sc = scaleFor(g.face);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f.info, (int)cp, &adv, &lsb);
        g.adv = adv * sc;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&f.info, (int)cp, sc, sc, &x0, &y0, &x1, &y1);
        g.w = x1 - x0;
        g.h = y1 - y0;
        g.xoff = (float)x0;
        g.yoff = (float)y0;
        if (g.w > 0 && g.h > 0 && g.w < atlasW_ && g.h < 128) {
            if (penX_ + g.w + 1 > atlasW_) {
                penX_ = 0;
                penY_ += rowH_ + 1;
                rowH_ = 0;
            }
            if (penY_ + g.h + 1 > atlasH_) {
                // Atlas full: start over (rare; may glitch one frame).
                resetAtlas();
                return glyph(cp);
            }
            tmp_.assign((size_t)g.w * g.h, 0);
            stbtt_MakeCodepointBitmap(&f.info, tmp_.data(), g.w, g.h, g.w, sc, sc, (int)cp);
            u8* dst = (u8*)atlas_.data;
            for (int y = 0; y < g.h; y++)
                for (int x = 0; x < g.w; x++)
                    dst[gfx::mortonOffset((u32)(penX_ + x), (u32)(penY_ + y), (u32)atlasW_)] = tmp_[y * g.w + x];
            g.sub.width = (u16)g.w;
            g.sub.height = (u16)g.h;
            g.sub.left = penX_ / (float)atlasW_;
            g.sub.right = (penX_ + g.w) / (float)atlasW_;
            g.sub.top = 1.0f - penY_ / (float)atlasH_;
            g.sub.bottom = 1.0f - (penY_ + g.h) / (float)atlasH_;
            penX_ += g.w + 1;
            if (g.h > rowH_) rowH_ = g.h;
            dirty_ = true;
        } else {
            g.w = g.h = 0;
        }
    }
    return cache_.emplace(key, g).first->second;
}

size_t FontRenderer::countChars(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    return n;
}

float FontRenderer::measure(const std::string& s) {
    float w = 0;
    size_t i = 0;
    while (i < s.size()) w += glyph(vn::utf8Next(s, i)).adv;
    return w;
}

static bool isCJK(uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x3000 && cp <= 0x30FF);
}

std::vector<std::string> FontRenderer::wrap(const std::string& s, float width) {
    std::vector<std::string> lines;
    size_t lineStart = 0, lastBreak = std::string::npos;
    float x = 0, xAtBreak = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t cpStart = i;
        uint32_t cp = vn::utf8Next(s, i);
        if (cp == '\n') {
            lines.push_back(s.substr(lineStart, cpStart - lineStart));
            lineStart = i;
            lastBreak = std::string::npos;
            x = 0;
            continue;
        }
        float adv = glyph(cp).adv;
        if (isCJK(cp) && cpStart > lineStart) {
            lastBreak = cpStart;
            xAtBreak = x;
        }
        if (x + adv > width && cpStart > lineStart) {
            if (lastBreak != std::string::npos && lastBreak > lineStart) {
                lines.push_back(s.substr(lineStart, lastBreak - lineStart));
                // skip the space we broke on
                lineStart = lastBreak;
                while (lineStart < s.size() && s[lineStart] == ' ') lineStart++;
                x = x - xAtBreak;
                // recompute width of the carried-over part exactly
                x = measure(s.substr(lineStart, cpStart - lineStart));
            } else {
                lines.push_back(s.substr(lineStart, cpStart - lineStart));
                lineStart = cpStart;
                x = 0;
            }
            lastBreak = std::string::npos;
        }
        x += adv;
        if (cp == ' ') {
            lastBreak = cpStart;
            xAtBreak = x - adv;
        }
    }
    lines.push_back(s.substr(lineStart));
    return lines;
}

void FontRenderer::draw(const std::string& s, float x, float y, u32 color, size_t maxChars) {
    if (!atlasOk_) return;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    float pen = x;
    float base = std::floor(y + ascent_);
    size_t i = 0, n = 0;
    while (i < s.size() && n < maxChars) {
        uint32_t cp = vn::utf8Next(s, i);
        n++;
        const Glyph& g = glyph(cp);
        if (g.w > 0) {
            C2D_Image img = {&atlas_, &g.sub};
            C2D_DrawImageAt(img, std::floor(pen + g.xoff + 0.5f), base + g.yoff, 0.5f, &tint, 1.0f, 1.0f);
        }
        pen += g.adv;
    }
    if (dirty_) {
        C3D_TexFlush(&atlas_);
        dirty_ = false;
    }
}
