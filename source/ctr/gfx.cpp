#include "gfx.h"

#include <vector>

namespace gfx {

static std::vector<C3D_Tex> g_garbage[2];
static int g_gen = 0;

void deferFree(C3D_Tex& tex) { g_garbage[g_gen].push_back(tex); }

void collectGarbage() {
    // Called right after C3D_FrameBegin(C3D_FRAME_SYNCDRAW): the previous frame is
    // finished, so textures queued two frames ago are definitely unused.
    int old = g_gen ^ 1;
    for (auto& t : g_garbage[old]) C3D_TexDelete(&t);
    g_garbage[old].clear();
    g_gen = old;
}

Texture::~Texture() {
    if (valid_) deferFree(tex_);
}

bool Texture::create(const vn::Image& im) {
    if (valid_) {
        deferFree(tex_);
        valid_ = false;
    }
    if (im.w <= 0 || im.h <= 0 || im.w > 1024 || im.h > 1024) return false;
    u16 tw = nextPow2((u16)im.w), th = nextPow2((u16)im.h);
    if (!C3D_TexInit(&tex_, tw, th, GPU_RGBA8)) return false;
    u32* dst = (u32*)tex_.data;
    memset(dst, 0, tex_.size);
    const u32* src = (const u32*)im.rgba.data();
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++)
            dst[mortonOffset((u32)x, (u32)y, tw)] = __builtin_bswap32(src[y * im.w + x]);  // RGBA -> ABGR
    C3D_TexFlush(&tex_);
    C3D_TexSetFilter(&tex_, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&tex_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sub_.width = (u16)im.w;
    sub_.height = (u16)im.h;
    sub_.left = 0.0f;
    sub_.top = 1.0f;
    sub_.right = im.w / (float)tw;
    sub_.bottom = 1.0f - im.h / (float)th;
    w_ = im.w;
    h_ = im.h;
    valid_ = true;
    return true;
}

void drawImage(const Texture& t, float x, float y, float alpha, float w, float h) {
    if (!t.valid() || alpha <= 0.0f) return;
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha > 1.0f ? 1.0f : alpha);
    float sx = w < 0 ? 1.0f : w / t.width();
    float sy = h < 0 ? 1.0f : h / t.height();
    C2D_DrawImageAt(t.image(), x, y, 0.5f, &tint, sx, sy);
}

void drawImageTint(const Texture& t, float x, float y, float alpha, float w, float h, u32 color, float blend) {
    if (!t.valid() || alpha <= 0.0f) return;
    u32 a = (u32)((alpha > 1.0f ? 1.0f : alpha) * 255.0f);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, (color & 0x00FFFFFF) | (a << 24), blend);
    float sx = w < 0 ? 1.0f : w / t.width();
    float sy = h < 0 ? 1.0f : h / t.height();
    C2D_DrawImageAt(t.image(), x, y, 0.5f, &tint, sx, sy);
}

void drawRect(float x, float y, float w, float h, u32 color) { C2D_DrawRectSolid(x, y, 0.5f, w, h, color); }

}  // namespace gfx
