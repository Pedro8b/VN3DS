// Audio decoders. Every format is decoded to interleaved signed 16-bit PCM.
// Supported: Ogg Vorbis, MP3, AAC (ADTS), WavPack, WAV (incl. ADPCM), FLAC.
// The format is detected from the file contents, not the extension, because
// converted VNDS ports often keep ".ogg"/".mp3" names on files of another type.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "vfs.h"

namespace vn {

class AudioDecoder {
public:
    virtual ~AudioDecoder() {}
    // Decode up to 'frames' frames into out (frames * channels samples).
    // Returns frames written; 0 means end of stream.
    virtual int read(int16_t* out, int frames) = 0;
    // Restart from the beginning (used for looping).
    virtual bool rewind() = 0;
    int channels() const { return channels_; }
    int rate() const { return rate_; }
    const char* format() const { return format_; }

protected:
    int channels_ = 0;
    int rate_ = 0;
    const char* format_ = "?";
};

// Takes ownership of the stream. Returns null if the format is unknown/broken.
std::unique_ptr<AudioDecoder> openAudio(std::unique_ptr<Stream> s, const std::string& nameHint = "");

}  // namespace vn
