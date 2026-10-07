// Image decoding (PNG, JPEG, BMP, GIF, TGA, PSD via stb_image) and high quality
// rescaling to the 3DS screen resolution.
#pragma once
#include <cstdint>
#include <vector>

#include "vfs.h"

namespace vn {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4, straight alpha
    bool hasAlpha = false;
};

bool decodeImage(Stream& s, Image& out);
// Reads only the header. Returns false if the format is unknown.
bool imageInfo(Stream& s, int& w, int& h);
// Area-average downscale / bilinear upscale, alpha-weighted (no dark halos).
Image scaleImage(const Image& src, int dw, int dh);

}  // namespace vn
