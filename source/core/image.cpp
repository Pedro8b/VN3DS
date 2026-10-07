#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../third_party/stb_image.h"

namespace vn {

static int stbRead(void* u, char* data, int size) { return (int)((Stream*)u)->read(data, (size_t)size); }
static void stbSkip(void* u, int n) { ((Stream*)u)->seek(n, SEEK_CUR); }
static int stbEof(void* u) {
    Stream* s = (Stream*)u;
    return s->tell() >= s->size();
}

bool decodeImage(Stream& s, Image& out) {
    stbi_io_callbacks cb = {stbRead, stbSkip, stbEof};
    int w, h, comp;
    stbi_uc* px;
    if (const uint8_t* mem = s.data()) {
        px = stbi_load_from_memory(mem, (int)s.size(), &w, &h, &comp, 4);
    } else {
        px = stbi_load_from_callbacks(&cb, &s, &w, &h, &comp, 4);
    }
    if (!px) {
        logf(LOG_WARN, "image decode failed: %s", stbi_failure_reason());
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    out.hasAlpha = false;
    if (comp == 2 || comp == 4) {
        for (size_t i = 3; i < out.rgba.size(); i += 4)
            if (out.rgba[i] != 255) {
                out.hasAlpha = true;
                break;
            }
    }
    return true;
}

bool imageInfo(Stream& s, int& w, int& h) {
    stbi_io_callbacks cb = {stbRead, stbSkip, stbEof};
    int comp;
    int64_t pos = s.tell();
    bool ok = stbi_info_from_callbacks(&cb, &s, &w, &h, &comp) != 0;
    s.seek(pos, SEEK_SET);
    return ok;
}

namespace {
struct Tap {
    int src;
    float w;
};
// For each destination index: list of source taps. Box filter when shrinking,
// tent (bilinear) when enlarging.
std::vector<std::vector<Tap>> makeTaps(int srcN, int dstN) {
    std::vector<std::vector<Tap>> taps(dstN);
    double scale = (double)srcN / dstN;
    for (int d = 0; d < dstN; d++) {
        auto& t = taps[d];
        if (scale > 1.0) {
            double a = d * scale, b = (d + 1) * scale;
            int i0 = (int)std::floor(a), i1 = std::min(srcN, (int)std::ceil(b));
            float sum = 0;
            for (int i = i0; i < i1; i++) {
                float w = (float)(std::min<double>(b, i + 1) - std::max<double>(a, i));
                if (w > 0) {
                    t.push_back({i, w});
                    sum += w;
                }
            }
            for (auto& x : t) x.w /= sum;
        } else {
            double c = (d + 0.5) * scale - 0.5;
            int i0 = (int)std::floor(c);
            float f = (float)(c - i0);
            int a = std::max(0, std::min(srcN - 1, i0));
            int b = std::max(0, std::min(srcN - 1, i0 + 1));
            if (a == b) t.push_back({a, 1.0f});
            else {
                t.push_back({a, 1.0f - f});
                t.push_back({b, f});
            }
        }
    }
    return taps;
}
}  // namespace

Image scaleImage(const Image& src, int dw, int dh) {
    Image out;
    if (dw <= 0 || dh <= 0 || src.w <= 0 || src.h <= 0) return out;
    out.w = dw;
    out.h = dh;
    out.hasAlpha = src.hasAlpha;
    if (dw == src.w && dh == src.h) {
        out.rgba = src.rgba;
        return out;
    }
    auto tx = makeTaps(src.w, dw);
    auto ty = makeTaps(src.h, dh);
    // Horizontal pass into premultiplied float rows.
    std::vector<float> tmp((size_t)dw * src.h * 4);
    for (int y = 0; y < src.h; y++) {
        const uint8_t* row = &src.rgba[(size_t)y * src.w * 4];
        float* o = &tmp[(size_t)y * dw * 4];
        for (int x = 0; x < dw; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (const Tap& t : tx[x]) {
                const uint8_t* p = row + t.src * 4;
                float wa = t.w * p[3];
                r += p[0] * wa;
                g += p[1] * wa;
                b += p[2] * wa;
                a += wa;
            }
            o[x * 4 + 0] = r;
            o[x * 4 + 1] = g;
            o[x * 4 + 2] = b;
            o[x * 4 + 3] = a;
        }
    }
    out.rgba.resize((size_t)dw * dh * 4);
    for (int y = 0; y < dh; y++) {
        uint8_t* o = &out.rgba[(size_t)y * dw * 4];
        for (int x = 0; x < dw; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            for (const Tap& t : ty[y]) {
                const float* p = &tmp[((size_t)t.src * dw + x) * 4];
                r += p[0] * t.w;
                g += p[1] * t.w;
                b += p[2] * t.w;
                a += p[3] * t.w;
            }
            if (a > 0.5f) {
                float inv = 1.0f / a;
                o[x * 4 + 0] = (uint8_t)std::min(255.0f, r * inv + 0.5f);
                o[x * 4 + 1] = (uint8_t)std::min(255.0f, g * inv + 0.5f);
                o[x * 4 + 2] = (uint8_t)std::min(255.0f, b * inv + 0.5f);
                o[x * 4 + 3] = (uint8_t)std::min(255.0f, a + 0.5f);
            } else {
                o[x * 4 + 0] = o[x * 4 + 1] = o[x * 4 + 2] = o[x * 4 + 3] = 0;
            }
        }
    }
    return out;
}

}  // namespace vn
