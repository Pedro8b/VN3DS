#include "audio_decoder.h"

#include <tremor/ivorbisfile.h>

#include <algorithm>
#include <cstring>
#include <vector>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../third_party/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../third_party/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "../third_party/dr_flac.h"

extern "C" {
#include "../third_party/faad2/neaacdec.h"
#include "../third_party/wavpack/wavpack.h"
}

namespace vn {

// ---------------------------------------------------------------------------
// dr_libs share one callback shape: read / seek(int, origin) / tell.

static size_t cbRead(void* u, void* out, size_t n) { return ((Stream*)u)->read(out, n); }
template <typename Origin>
static unsigned int cbSeek(void* u, int off, Origin origin) {
    int wh = (int)origin == 0 ? SEEK_SET : (int)origin == 1 ? SEEK_CUR : SEEK_END;
    return ((Stream*)u)->seek(off, wh) ? 1 : 0;
}
template <typename I64>
static unsigned int cbTell(void* u, I64* cur) {
    *cur = ((Stream*)u)->tell();
    return 1;
}

class Mp3Decoder : public AudioDecoder {
public:
    explicit Mp3Decoder(std::unique_ptr<Stream> s) : s_(std::move(s)) {}
    ~Mp3Decoder() override {
        if (ok_) drmp3_uninit(&mp3_);
    }
    bool init() {
        ok_ = drmp3_init(&mp3_, cbRead, cbSeek<drmp3_seek_origin>, cbTell<drmp3_int64>, nullptr, s_.get(), nullptr);
        if (!ok_) return false;
        channels_ = (int)mp3_.channels;
        rate_ = (int)mp3_.sampleRate;
        format_ = "mp3";
        return channels_ > 0 && rate_ > 0;
    }
    int read(int16_t* out, int frames) override { return (int)drmp3_read_pcm_frames_s16(&mp3_, frames, out); }
    bool rewind() override { return drmp3_seek_to_pcm_frame(&mp3_, 0); }

private:
    std::unique_ptr<Stream> s_;
    drmp3 mp3_;
    bool ok_ = false;
};

class WavDecoder : public AudioDecoder {
public:
    explicit WavDecoder(std::unique_ptr<Stream> s) : s_(std::move(s)) {}
    ~WavDecoder() override {
        if (ok_) drwav_uninit(&wav_);
    }
    bool init() {
        ok_ = drwav_init(&wav_, cbRead, cbSeek<drwav_seek_origin>, cbTell<drwav_int64>, s_.get(), nullptr);
        if (!ok_) return false;
        channels_ = wav_.channels;
        rate_ = (int)wav_.sampleRate;
        format_ = "wav";
        return channels_ > 0 && rate_ > 0;
    }
    int read(int16_t* out, int frames) override { return (int)drwav_read_pcm_frames_s16(&wav_, frames, out); }
    bool rewind() override { return drwav_seek_to_pcm_frame(&wav_, 0); }

private:
    std::unique_ptr<Stream> s_;
    drwav wav_;
    bool ok_ = false;
};

class FlacDecoder : public AudioDecoder {
public:
    explicit FlacDecoder(std::unique_ptr<Stream> s) : s_(std::move(s)) {}
    ~FlacDecoder() override {
        if (f_) drflac_close(f_);
    }
    bool init() {
        f_ = drflac_open(cbRead, cbSeek<drflac_seek_origin>, cbTell<drflac_int64>, s_.get(), nullptr);
        if (!f_) return false;
        channels_ = f_->channels;
        rate_ = (int)f_->sampleRate;
        format_ = "flac";
        return true;
    }
    int read(int16_t* out, int frames) override { return (int)drflac_read_pcm_frames_s16(f_, frames, out); }
    bool rewind() override { return drflac_seek_to_pcm_frame(f_, 0); }

private:
    std::unique_ptr<Stream> s_;
    drflac* f_ = nullptr;
};

// ---------------------------------------------------------------------------
// Ogg Vorbis (Tremor, integer decoder - fast on the 3DS' ARM11)

static size_t ovRead(void* ptr, size_t size, size_t nmemb, void* ds) {
    if (size == 0) return 0;
    return ((Stream*)ds)->read(ptr, size * nmemb) / size;
}
static int ovSeek(void* ds, ogg_int64_t off, int whence) { return ((Stream*)ds)->seek(off, whence) ? 0 : -1; }
static long ovTell(void* ds) { return (long)((Stream*)ds)->tell(); }

class VorbisDecoder : public AudioDecoder {
public:
    explicit VorbisDecoder(std::unique_ptr<Stream> s) : s_(std::move(s)) {}
    ~VorbisDecoder() override {
        if (ok_) ov_clear(&vf_);
    }
    bool init() {
        ov_callbacks cb;
        cb.read_func = ovRead;
        cb.seek_func = ovSeek;
        cb.close_func = nullptr;
        cb.tell_func = ovTell;
        if (ov_open_callbacks(s_.get(), &vf_, nullptr, 0, cb) != 0) return false;
        ok_ = true;
        vorbis_info* vi = ov_info(&vf_, -1);
        if (!vi) return false;
        channels_ = vi->channels;
        rate_ = (int)vi->rate;
        format_ = "ogg";
        return channels_ > 0 && channels_ <= 2 && rate_ > 0;
    }
    int read(int16_t* out, int frames) override {
        int want = frames * channels_ * 2, got = 0;
        while (got < want) {
            int bs = 0;
            long r = ov_read(&vf_, (char*)out + got, want - got, &bs);
            if (r == OV_HOLE) continue;
            if (r <= 0) break;
            got += (int)r;
        }
        return got / (channels_ * 2);
    }
    bool rewind() override { return ov_pcm_seek(&vf_, 0) == 0; }

private:
    std::unique_ptr<Stream> s_;
    OggVorbis_File vf_;
    bool ok_ = false;
};

// ---------------------------------------------------------------------------
// WavPack

static int32_t wvRead(void* id, void* data, int32_t n) { return (int32_t)((Stream*)id)->read(data, n); }
static int32_t wvWrite(void*, void*, int32_t) { return 0; }
static int64_t wvGetPos(void* id) { return ((Stream*)id)->tell(); }
static int wvSetAbs(void* id, int64_t pos) { return ((Stream*)id)->seek(pos, SEEK_SET) ? 0 : -1; }
static int wvSetRel(void* id, int64_t d, int mode) { return ((Stream*)id)->seek(d, mode) ? 0 : -1; }
static int wvPushBack(void* id, int c) { return ((Stream*)id)->seek(-1, SEEK_CUR) ? c : EOF; }
static int64_t wvLen(void* id) { return ((Stream*)id)->size(); }
static int wvCanSeek(void*) { return 1; }
static int wvTrunc(void*) { return -1; }
static int wvClose(void*) { return 0; }

class WavPackDecoder : public AudioDecoder {
public:
    explicit WavPackDecoder(std::unique_ptr<Stream> s) : s_(std::move(s)) {}
    ~WavPackDecoder() override {
        if (wpc_) WavpackCloseFile(wpc_);
    }
    bool init() {
        rd_.read_bytes = wvRead;
        rd_.write_bytes = wvWrite;
        rd_.get_pos = wvGetPos;
        rd_.set_pos_abs = wvSetAbs;
        rd_.set_pos_rel = wvSetRel;
        rd_.push_back_byte = wvPushBack;
        rd_.get_length = wvLen;
        rd_.can_seek = wvCanSeek;
        rd_.truncate_here = wvTrunc;
        rd_.close = wvClose;
        char err[80] = {0};
        wpc_ = WavpackOpenFileInputEx64(&rd_, s_.get(), nullptr, err, OPEN_NORMALIZE | OPEN_DSD_AS_PCM, 0);
        if (!wpc_) {
            logf(LOG_WARN, "wavpack: %s", err);
            return false;
        }
        srcCh_ = WavpackGetNumChannels(wpc_);
        channels_ = std::min(srcCh_, 2);
        rate_ = (int)WavpackGetSampleRate(wpc_);
        bps_ = WavpackGetBytesPerSample(wpc_);
        isFloat_ = (WavpackGetMode(wpc_) & MODE_FLOAT) != 0;
        format_ = "wavpack";
        return channels_ > 0 && rate_ > 0;
    }
    int read(int16_t* out, int frames) override {
        tmp_.resize((size_t)frames * srcCh_);
        uint32_t got = WavpackUnpackSamples(wpc_, tmp_.data(), (uint32_t)frames);
        for (uint32_t f = 0; f < got; f++) {
            for (int c = 0; c < channels_; c++) {
                int32_t v = tmp_[f * srcCh_ + c];
                int32_t o;
                if (isFloat_) {
                    float fv;
                    memcpy(&fv, &v, 4);
                    o = (int32_t)(fv * 32767.0f);
                } else if (bps_ == 1) o = v << 8;
                else if (bps_ == 2) o = v;
                else if (bps_ == 3) o = v >> 8;
                else o = v >> 16;
                out[f * channels_ + c] = (int16_t)std::max<int32_t>(-32768, std::min<int32_t>(32767, o));
            }
        }
        return (int)got;
    }
    bool rewind() override { return WavpackSeekSample64(wpc_, 0) != 0; }

private:
    std::unique_ptr<Stream> s_;
    WavpackStreamReader64 rd_;
    WavpackContext* wpc_ = nullptr;
    int srcCh_ = 0, bps_ = 2;
    bool isFloat_ = false;
    std::vector<int32_t> tmp_;
};

// ---------------------------------------------------------------------------
// AAC: raw ADTS streams and AAC inside MP4/M4A containers (FAAD2)

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t* p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

class AacDecoder : public AudioDecoder {
public:
    AacDecoder(std::unique_ptr<Stream> s, bool mp4) : s_(std::move(s)), mp4_(mp4) {}
    ~AacDecoder() override {
        if (h_) NeAACDecClose(h_);
    }

    bool init() {
        format_ = mp4_ ? "m4a" : "aac";
        if (mp4_ && !parseMp4()) return false;
        if (!start()) return false;
        // Decode the first frame now: HE-AAC (SBR/PS) only reveals its real rate and
        // channel count after decoding.
        if (!decodeFrame()) return false;
        return channels_ > 0 && rate_ > 0;
    }

    int read(int16_t* out, int frames) override {
        int done = 0;
        while (done < frames) {
            if (pendPos_ >= pend_.size()) {
                pend_.clear();
                pendPos_ = 0;
                if (!decodeFrame()) break;
                continue;
            }
            size_t avail = (pend_.size() - pendPos_) / channels_;
            size_t n = std::min<size_t>(avail, (size_t)(frames - done));
            memcpy(out + done * channels_, &pend_[pendPos_], n * channels_ * 2);
            pendPos_ += n * channels_;
            done += (int)n;
        }
        return done;
    }

    bool rewind() override {
        pend_.clear();
        pendPos_ = 0;
        return start();
    }

private:
    bool start() {
        if (h_) NeAACDecClose(h_);
        h_ = NeAACDecOpen();
        NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(h_);
        cfg->outputFormat = FAAD_FMT_16BIT;
        cfg->downMatrix = 1;
        cfg->dontUpSampleImplicitSBR = 1;  // LC at <=24kHz: no pointless 2x upsampling
        NeAACDecSetConfiguration(h_, cfg);
        unsigned long rate = 0;
        unsigned char ch = 0;
        if (mp4_) {
            sample_ = 0;
            if (NeAACDecInit2(h_, asc_.data(), (unsigned long)asc_.size(), &rate, &ch) < 0) return false;
        } else {
            s_->seek(0, SEEK_SET);
            buf_.clear();
            bufPos_ = 0;
            eof_ = false;
            refill();
            skipId3();
            long skip = NeAACDecInit(h_, buf_.data() + bufPos_, (unsigned long)(buf_.size() - bufPos_), &rate, &ch);
            if (skip < 0) return false;
            bufPos_ += (size_t)skip;
        }
        if (!rate_) {
            rate_ = (int)rate;
            channels_ = std::min<int>(ch, 2);
        }
        return true;
    }

    void refill() {
        if (bufPos_ > 0) {
            buf_.erase(buf_.begin(), buf_.begin() + bufPos_);
            bufPos_ = 0;
        }
        size_t want = 16384;
        if (buf_.size() >= want || eof_) return;
        size_t old = buf_.size();
        buf_.resize(want);
        size_t got = s_->read(buf_.data() + old, want - old);
        buf_.resize(old + got);
        if (got == 0) eof_ = true;
    }

    void skipId3() {
        if (buf_.size() - bufPos_ >= 10 && memcmp(&buf_[bufPos_], "ID3", 3) == 0) {
            const uint8_t* p = &buf_[bufPos_];
            size_t sz = ((p[6] & 0x7f) << 21) | ((p[7] & 0x7f) << 14) | ((p[8] & 0x7f) << 7) | (p[9] & 0x7f);
            sz += 10;
            if (sz <= buf_.size() - bufPos_) {
                bufPos_ += sz;
            } else {
                s_->seek((int64_t)sz, SEEK_SET);
                buf_.clear();
                bufPos_ = 0;
                refill();
            }
        }
    }

    // Decodes one AAC frame into pend_. Returns false at end of stream.
    bool decodeFrame() {
        for (int attempts = 0; attempts < 64; attempts++) {
            NeAACDecFrameInfo info;
            void* pcm;
            if (mp4_) {
                if (sample_ >= sizes_.size()) return false;
                std::vector<uint8_t> frame(sizes_[sample_]);
                s_->seek((int64_t)offsets_[sample_], SEEK_SET);
                if (s_->read(frame.data(), frame.size()) != frame.size()) return false;
                sample_++;
                pcm = NeAACDecDecode(h_, &info, frame.data(), (unsigned long)frame.size());
            } else {
                if (buf_.size() - bufPos_ < FAAD_MIN_STREAMSIZE * 2) refill();
                if (bufPos_ >= buf_.size()) return false;
                pcm = NeAACDecDecode(h_, &info, buf_.data() + bufPos_, (unsigned long)(buf_.size() - bufPos_));
                bufPos_ += info.bytesconsumed;
                if (info.error && info.bytesconsumed == 0) {
                    // resync: skip a byte and look for the next ADTS header
                    bufPos_++;
                    while (bufPos_ + 1 < buf_.size() &&
                           !(buf_[bufPos_] == 0xFF && (buf_[bufPos_ + 1] & 0xF6) == 0xF0))
                        bufPos_++;
                }
            }
            if (info.error) continue;
            if (info.samples == 0) continue;
            int ch = info.channels;
            if (info.samplerate) rate_ = (int)info.samplerate;
            if (ch <= 0) continue;
            int outCh = std::min(ch, 2);
            if (channels_ == 0 || pendFirst_) channels_ = outCh;
            pendFirst_ = false;
            const int16_t* src = (const int16_t*)pcm;
            size_t frames = info.samples / ch;
            size_t base = pend_.size();
            pend_.resize(base + frames * channels_);
            for (size_t f = 0; f < frames; f++) {
                if (channels_ == 1) {
                    pend_[base + f] = src[f * ch];
                } else {
                    pend_[base + f * 2] = src[f * ch];
                    pend_[base + f * 2 + 1] = src[f * ch + (ch > 1 ? 1 : 0)];
                }
            }
            return true;
        }
        return false;
    }

    // Minimal MP4 demuxer: first audio track's AudioSpecificConfig + sample table.
    bool readBox(int64_t& pos, int64_t end, uint32_t& type, int64_t& bodyStart, int64_t& bodyEnd) {
        if (pos + 8 > end) return false;
        uint8_t h[16];
        s_->seek(pos, SEEK_SET);
        if (s_->read(h, 8) != 8) return false;
        uint64_t size = be32(h);
        type = be32(h + 4);
        int64_t hdr = 8;
        if (size == 1) {
            if (s_->read(h + 8, 8) != 8) return false;
            size = be64(h + 8);
            hdr = 16;
        } else if (size == 0) {
            size = (uint64_t)(end - pos);
        }
        if (size < (uint64_t)hdr) return false;
        bodyStart = pos + hdr;
        bodyEnd = pos + (int64_t)size;
        pos = bodyEnd;
        return bodyEnd <= end;
    }

    static constexpr uint32_t fourcc(const char* s) {
        return ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | (uint32_t)s[3];
    }

    bool readBody(int64_t a, int64_t b, std::vector<uint8_t>& out) {
        out.resize((size_t)(b - a));
        s_->seek(a, SEEK_SET);
        return s_->read(out.data(), out.size()) == out.size();
    }

    // Walks containers looking for an mp4a track; fills asc_/sizes_/offsets_.
    bool walk(int64_t start, int64_t end) {
        int64_t pos = start;
        uint32_t type;
        int64_t a, b;
        while (readBox(pos, end, type, a, b)) {
            if (type == fourcc("moov") || type == fourcc("mdia") || type == fourcc("minf") || type == fourcc("stbl")) {
                if (walk(a, b)) return true;
            } else if (type == fourcc("trak")) {
                asc_.clear();
                sizes_.clear();
                offsets_.clear();
                stsc_.clear();
                chunkOffs_.clear();
                walk(a, b);
                if (!asc_.empty() && !sizes_.empty() && !chunkOffs_.empty()) {
                    buildOffsets();
                    return true;
                }
            } else if (type == fourcc("stsd")) {
                std::vector<uint8_t> d;
                if (!readBody(a, b, d)) return false;
                findEsds(d);
            } else if (type == fourcc("stsz")) {
                std::vector<uint8_t> d;
                if (!readBody(a, b, d) || d.size() < 12) return false;
                uint32_t fixed = be32(&d[4]), n = be32(&d[8]);
                sizes_.resize(n);
                for (uint32_t i = 0; i < n; i++)
                    sizes_[i] = fixed ? fixed : (12 + i * 4 + 4 <= d.size() ? be32(&d[12 + i * 4]) : 0);
            } else if (type == fourcc("stco") || type == fourcc("co64")) {
                std::vector<uint8_t> d;
                if (!readBody(a, b, d) || d.size() < 8) return false;
                uint32_t n = be32(&d[4]);
                bool big = type == fourcc("co64");
                for (uint32_t i = 0; i < n; i++) {
                    size_t o = 8 + i * (big ? 8 : 4);
                    if (o + (big ? 8 : 4) > d.size()) break;
                    chunkOffs_.push_back(big ? be64(&d[o]) : be32(&d[o]));
                }
            } else if (type == fourcc("stsc")) {
                std::vector<uint8_t> d;
                if (!readBody(a, b, d) || d.size() < 8) return false;
                uint32_t n = be32(&d[4]);
                for (uint32_t i = 0; i < n && 8 + i * 12 + 12 <= d.size(); i++)
                    stsc_.push_back({be32(&d[8 + i * 12]), be32(&d[8 + i * 12 + 4])});
            }
        }
        return false;
    }

    void findEsds(const std::vector<uint8_t>& d) {
        // search for the DecoderSpecificInfo descriptor (tag 0x05) inside 'esds'
        for (size_t i = 0; i + 8 < d.size(); i++) {
            if (memcmp(&d[i], "esds", 4) != 0) continue;
            for (size_t j = i + 8; j + 2 < d.size(); j++) {
                if (d[j] != 0x05) continue;
                size_t k = j + 1, len = 0;
                for (int q = 0; q < 4 && k < d.size(); q++) {
                    len = (len << 7) | (d[k] & 0x7f);
                    if (!(d[k++] & 0x80)) break;
                }
                if (len >= 2 && len < 64 && k + len <= d.size()) {
                    asc_.assign(d.begin() + k, d.begin() + k + len);
                    return;
                }
            }
        }
    }

    void buildOffsets() {
        offsets_.resize(sizes_.size());
        size_t s = 0;
        for (size_t c = 0; c < chunkOffs_.size() && s < sizes_.size(); c++) {
            uint32_t per = 1;
            for (auto& e : stsc_)
                if (e.first <= c + 1) per = e.second;
            uint64_t off = chunkOffs_[c];
            for (uint32_t k = 0; k < per && s < sizes_.size(); k++) {
                offsets_[s] = off;
                off += sizes_[s];
                s++;
            }
        }
        sizes_.resize(s);
        offsets_.resize(s);
    }

    bool parseMp4() { return walk(0, s_->size()); }

    std::unique_ptr<Stream> s_;
    bool mp4_;
    NeAACDecHandle h_ = nullptr;
    std::vector<uint8_t> buf_;
    size_t bufPos_ = 0;
    bool eof_ = false;
    std::vector<int16_t> pend_;
    size_t pendPos_ = 0;
    bool pendFirst_ = true;
    // mp4
    std::vector<uint8_t> asc_;
    std::vector<uint32_t> sizes_;
    std::vector<uint64_t> offsets_, chunkOffs_;
    std::vector<std::pair<uint32_t, uint32_t>> stsc_;
    size_t sample_ = 0;
};

// ---------------------------------------------------------------------------

template <typename T, typename... Args>
static std::unique_ptr<AudioDecoder> tryInit(Args&&... args) {
    std::unique_ptr<T> d(new T(std::forward<Args>(args)...));
    if (!d->init()) return nullptr;
    return std::unique_ptr<AudioDecoder>(d.release());
}

std::unique_ptr<AudioDecoder> openAudio(std::unique_ptr<Stream> s, const std::string& nameHint) {
    if (!s) return nullptr;
    uint8_t h[64] = {0};
    size_t n = s->read(h, sizeof(h));
    // Look past an ID3v2 tag to classify MP3 vs ADTS AAC.
    size_t id3 = 0;
    if (n >= 10 && memcmp(h, "ID3", 3) == 0)
        id3 = 10 + (((h[6] & 0x7f) << 21) | ((h[7] & 0x7f) << 14) | ((h[8] & 0x7f) << 7) | (h[9] & 0x7f));
    uint8_t sync[2] = {h[0], h[1]};
    if (id3) {
        s->seek((int64_t)id3, SEEK_SET);
        if (s->read(sync, 2) != 2) sync[0] = sync[1] = 0;
    }
    s->seek(0, SEEK_SET);

    std::string ext = fileExt(nameHint);
    if (n >= 4 && memcmp(h, "OggS", 4) == 0) {
        bool opus = false;
        for (size_t i = 0; i + 8 <= n; i++)
            if (memcmp(h + i, "OpusHead", 8) == 0) opus = true;
        if (opus) {
            logf(LOG_WARN, "opus audio is not supported yet: %s", nameHint.c_str());
            return nullptr;
        }
        return tryInit<VorbisDecoder>(std::move(s));
    }
    if (n >= 4 && (memcmp(h, "RIFF", 4) == 0 || memcmp(h, "RIFX", 4) == 0 || memcmp(h, "RF64", 4) == 0 ||
                   memcmp(h, "riff", 4) == 0 || memcmp(h, "FORM", 4) == 0))
        return tryInit<WavDecoder>(std::move(s));
    if (n >= 4 && memcmp(h, "fLaC", 4) == 0) return tryInit<FlacDecoder>(std::move(s));
    if (n >= 4 && memcmp(h, "wvpk", 4) == 0) return tryInit<WavPackDecoder>(std::move(s));
    if (n >= 8 && memcmp(h + 4, "ftyp", 4) == 0) return tryInit<AacDecoder>(std::move(s), true);
    if (sync[0] == 0xFF && (sync[1] & 0xF6) == 0xF0) return tryInit<AacDecoder>(std::move(s), false);
    if (sync[0] == 0xFF && (sync[1] & 0xE0) == 0xE0) return tryInit<Mp3Decoder>(std::move(s));
    // Unknown magic: fall back on the extension.
    if (ext == "aac" || ext == "adts") return tryInit<AacDecoder>(std::move(s), false);
    if (ext == "mp3") return tryInit<Mp3Decoder>(std::move(s));
    logf(LOG_WARN, "unknown audio format: %s", nameHint.c_str());
    return nullptr;
}

}  // namespace vn
