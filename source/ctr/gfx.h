// citro2d helpers: RGBA textures uploaded from vn::Image, deferred deletion, primitives.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <memory>

#include "../core/image.h"

namespace gfx {

constexpr int TOP_W = 400, TOP_H = 240, BOT_W = 320, BOT_H = 240;

class Texture {
public:
    Texture() {}
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    bool create(const vn::Image& im);  // image must be <= 1024x1024
    bool valid() const { return valid_; }
    int width() const { return w_; }
    int height() const { return h_; }
    C2D_Image image() const { return C2D_Image{const_cast<C3D_Tex*>(&tex_), &sub_}; }
    size_t bytes() const { return valid_ ? tex_.size : 0; }

private:
    C3D_Tex tex_;
    Tex3DS_SubTexture sub_;
    int w_ = 0, h_ = 0;
    bool valid_ = false;
};
using TexPtr = std::shared_ptr<Texture>;

// Morton offset inside a tiled 3DS texture of width 'texW' (power of two).
static inline u32 mortonOffset(u32 x, u32 y, u32 texW) {
    return ((((y >> 3) * (texW >> 3) + (x >> 3)) << 6) +
            ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
}
static inline u16 nextPow2(u16 v) {
    u16 p = 8;
    while (p < v) p <<= 1;
    return p;
}

// Textures freed while the GPU may still read them go here and are released at the
// start of the next frame.
void deferFree(C3D_Tex& tex);
void collectGarbage();

void drawImage(const Texture& t, float x, float y, float alpha = 1.0f, float w = -1, float h = -1);
void drawRect(float x, float y, float w, float h, u32 color);
// Draw with a colour tint (blend 0..1) - used with C2D_SetTintMode for film effects.
void drawImageTint(const Texture& t, float x, float y, float alpha, float w, float h, u32 color, float blend);
inline u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) { return C2D_Color32(r, g, b, a); }

}  // namespace gfx
