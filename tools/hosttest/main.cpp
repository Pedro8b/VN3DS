// Host-side test harness for the platform independent core.
//   hosttest scan <dir>...           list novels found in the given roots
//   hosttest run <novel> [steps]     run the script headless, resolving + decoding every resource
//   hosttest audio <file> [out.wav]  decode an audio file (optionally to wav)
//   hosttest image <file> W H out.ppm
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>

#include "../../source/core/audio_decoder.h"
#include "../../source/core/higurashi.h"
#include "../../source/core/image.h"
#include "../../source/core/save.h"
#include "../../source/core/script.h"
#include "../../source/core/vfs.h"

using namespace vn;

static int decodeAll(AudioDecoder& d, long* framesOut, FILE* wav) {
    std::vector<int16_t> buf(4096 * 2);
    long total = 0;
    for (;;) {
        int n = d.read(buf.data(), 4096);
        if (n <= 0) break;
        if (wav) fwrite(buf.data(), 2, (size_t)n * d.channels(), wav);
        total += n;
    }
    *framesOut = total;
    return 0;
}

static void wavHeader(FILE* f, int ch, int rate, long frames) {
    uint32_t data = (uint32_t)(frames * ch * 2);
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16((uint16_t)ch); w32((uint32_t)rate); w32((uint32_t)rate * ch * 2); w16((uint16_t)(ch * 2)); w16(16);
    fwrite("data", 1, 4, f); w32(data);
}

struct TestHost : ScriptHost {
    Novel& novel;
    bool decode;
    std::set<std::string> seenImg, seenSnd, missing, failed;
    int texts = 0, choices = 0, bgs = 0, sprites = 0, sounds = 0, musics = 0;
    std::string lastErr;
    TestHost(Novel& n, bool d) : novel(n), decode(d) {}

    void checkImage(ResCat cat, const std::string& p) {
        if (p.empty() || p == "~") return;
        std::string key = std::to_string(cat) + p;
        if (!seenImg.insert(key).second) return;
        std::string res;
        auto s = novel.openRes(cat, p, &res);
        if (!s) {
            missing.insert((cat == RES_BACKGROUND ? "bg: " : "fg: ") + p);
            return;
        }
        if (!decode || s->size() == 0) return;
        Image img;
        if (!decodeImage(*s, img)) {
            failed.insert(p + " -> " + res);
            return;
        }
        if (cat == RES_BACKGROUND && !novel.hasImgIni() && bgs == 1 && (img.w != 256 || img.h != 192)) {
            printf("  (no img.ini: base size guessed from first background %dx%d)\n", img.w, img.h);
            novel.setBaseSize(img.w, img.h);
        }
        float sc = std::min(400.0f / novel.baseWidth(), 240.0f / novel.baseHeight());
        Image out = scaleImage(img, std::max(1, (int)(img.w * sc + 0.5f)), std::max(1, (int)(img.h * sc + 0.5f)));
        (void)out;
    }
    void checkSound(const std::string& p) {
        if (p.empty() || p == "~") return;
        if (!seenSnd.insert(p).second) return;
        std::string res;
        auto s = novel.openRes(RES_SOUND, p, &res);
        if (!s) {
            missing.insert("snd: " + p);
            return;
        }
        if (!decode || s->size() == 0) return;
        auto d = openAudio(std::move(s), res);
        if (!d) {
            failed.insert(p + " -> " + res);
            return;
        }
        std::vector<int16_t> buf(2048 * 2);
        int n = d->read(buf.data(), 2048);
        if (n <= 0) failed.insert(p + " (no samples) -> " + res);
    }

    void setBackground(const std::string& p, int) override { dirty = true; bgs++; checkImage(RES_BACKGROUND, p); }
    void addSprite(const std::string& p, int, int) override { dirty = true; sprites++; checkImage(RES_FOREGROUND, p); }
    bool dirty = false;
    bool flushGraphics(bool instant) override { bool d = dirty; dirty = false; return d && !instant; }
    void playSound(const std::string& p, int) override { sounds++; checkSound(p); }
    void playMusic(const std::string& p) override { musics++; checkSound(p); }
    void appendText(const std::string& t) override {
        texts++;
        if (getenv("HT_TEXT")) printf("TEXT[%s]\n", t.c_str());
    }
    void clearText(bool) override {}
    void showChoice(const std::vector<std::string>& o) override {
        choices++;
        (void)o;
    }
    void globalsChanged() override {}
    void setLayer(int, const std::string& p, int, int, int) override { dirty = true; sprites++; checkImage(RES_FOREGROUND, p); }
    void clearLayer(int) override { dirty = true; }
    void clearLayers() override { dirty = true; }
    void setBackgroundKeep(const std::string& p) override { dirty = true; bgs++; checkImage(RES_BACKGROUND, p); }
    bool flushGraphicsFor(int frames) override { bool d = dirty; dirty = false; return d && frames > 0; }
    void playAudio(int kind, int, const std::string& p, float, int) override { if (kind == 0) musics++; else sounds++; checkSound(p); }
    void stopAudio(int, int, int) override {}
    void appendInline(const std::string& t) override { inl++; lastInline = t; }
    int inl = 0;
    std::string lastInline;
    void scriptError(const std::string& m) override {
        lastErr = m;
        printf("  SCRIPT ERROR: %s\n", m.c_str());
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: hosttest scan|run|audio|image ...\n");
        return 2;
    }
    std::string mode = argv[1];
    if (mode == "scan") {
        std::vector<std::string> roots(argv + 2, argv + argc);
        for (auto& e : Novel::scan(roots)) printf("%-40s [%s] %s\n", e.title.c_str(), e.layout.c_str(), e.path.c_str());
        return 0;
    }
    if (mode == "audio") {
        clock_t t0 = clock();
        auto s = FileStream::open(argv[2]);
        auto d = openAudio(std::move(s), argv[2]);
        if (!d) {
            printf("FAIL %s\n", argv[2]);
            return 1;
        }
        FILE* wav = argc > 3 ? fopen(argv[3], "wb") : nullptr;
        if (wav) wavHeader(wav, d->channels(), d->rate(), 0);
        long frames;
        decodeAll(*d, &frames, wav);
        bool rw = d->rewind();
        std::vector<int16_t> b(1024 * 2);
        int again = d->read(b.data(), 1024);
        if (wav) {
            fseek(wav, 0, SEEK_SET);
            wavHeader(wav, d->channels(), d->rate(), frames);
            fclose(wav);
        }
        printf("OK %s fmt=%s ch=%d rate=%d frames=%ld (%.2fs) rewind=%d/%d decode=%.0fms\n", argv[2], d->format(),
               d->channels(), d->rate(), frames, frames / (double)d->rate(), rw, again,
               (clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
        return 0;
    }
    if (mode == "image") {
        auto s = FileStream::open(argv[2]);
        Image img;
        if (!s || !decodeImage(*s, img)) {
            printf("FAIL\n");
            return 1;
        }
        int w = atoi(argv[3]), h = atoi(argv[4]);
        Image o = scaleImage(img, w, h);
        FILE* f = fopen(argv[5], "wb");
        fprintf(f, "P6\n%d %d\n255\n", o.w, o.h);
        for (int i = 0; i < o.w * o.h; i++) {
            // composite on magenta to show alpha edges
            int a = o.rgba[i * 4 + 3];
            unsigned char px[3];
            const int bg[3] = {255, 0, 255};
            for (int c = 0; c < 3; c++) px[c] = (unsigned char)((o.rgba[i * 4 + c] * a + bg[c] * (255 - a)) / 255);
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        printf("OK %dx%d -> %dx%d alpha=%d\n", img.w, img.h, o.w, o.h, img.hasAlpha);
        return 0;
    }
    if (mode == "run") {
        setLogLevel(LOG_WARN);
        Novel novel;
        if (!novel.open(argv[2])) {
            printf("cannot open novel\n");
            return 1;
        }
        printf("title=%s layout=[%s] base=%dx%d\n%s", novel.title().c_str(), novel.layout().c_str(),
               novel.baseWidth(), novel.baseHeight(), novel.describe().c_str());
        long steps = argc > 3 ? atol(argv[3]) : 200000;
        bool decode = argc > 4 ? atoi(argv[4]) != 0 : true;
        TestHost host(novel, decode);
        std::unique_ptr<Engine> engp;
        if (novel.engine() == ENGINE_HIGURASHI) engp.reset(new HigurashiEngine(novel, host));
        else engp.reset(new ScriptEngine(novel, host));
        Engine& eng = *engp;
        printf("engine=%s main=%s chapters=%zu tips=%zu\n", novel.engine() == ENGINE_HIGURASHI ? "higurashi" : "vnds",
               novel.mainScript().c_str(), eng.chapters().size(), eng.tips().size());
        srand(1);
        if (!eng.start()) return 1;
        SaveState mid;
        long midAt = steps / 3;
        long i = 0;
        int choiceRound = 0;
        long fx = 0;
        std::set<std::string> filesVisited;
        for (; i < steps; i++) {
            filesVisited.insert(eng.currentFile());
            if (i >= midAt && mid.file.empty() && eng.state() == Engine::WAIT_INPUT) mid = eng.save();
            auto st = eng.run(false);
            if (i >= midAt && mid.file.empty() && st == Engine::WAIT_INPUT) mid = eng.save();
            if (st == Engine::ENDED) break;
            if (st == Engine::WAIT_INPUT) eng.advance();
            else if (st == Engine::WAIT_DELAY) eng.cancelDelay();
            else if (st == Engine::WAIT_FX) { fx++; eng.resume(); }
            else if (st == Engine::WAIT_CHOICE) eng.choose((choiceRound++) % 2);
        }
        // save/load round trip
        SaveState s = eng.save();
        writeSave("/tmp/hosttest_save.sav", s);
        SaveState r;
        bool rl = readSave("/tmp/hosttest_save.sav", r);
        printf("steps=%ld state=%d file=%s line=%d texts=%d choices=%d bg=%d fg=%d snd=%d mus=%d scripts=%zu\n", i,
               (int)eng.state(), eng.currentFile().c_str(), eng.currentLine(), host.texts, host.choices, host.bgs,
               host.sprites, host.sounds, host.musics, filesVisited.size());
        printf("fx waits=%ld inline=%d last='%s'\n", fx, host.inl, host.lastInline.substr(0, 80).c_str());
        if (!mid.file.empty()) {
            // load test: replay to the saved position and check we stop there
            int beforeTexts = host.inl;
            bool ok = eng.load(mid);
            int guard = 0;
            Engine::State st2 = Engine::RUNNING;
            while (ok && guard++ < 100000) {
                st2 = eng.run(false);
                if (st2 == Engine::WAIT_FX) { eng.resume(); continue; }
                if (st2 == Engine::WAIT_DELAY) { eng.cancelDelay(); continue; }
                break;
            }
            printf("load test: saved %s@%d -> state=%d at %s@%d text='%s' (appends %d)\n", mid.file.c_str(), mid.textPos,
                   (int)st2, eng.currentFile().c_str(), eng.currentLine(), eng.lastText().substr(0, 60).c_str(),
                   host.inl - beforeTexts);
        }
        printf("save roundtrip=%d file=%s line=%d vars=%zu\n", rl, r.file.c_str(), r.line, r.vars.size());
        printf("unique images=%zu sounds=%zu missing=%zu failed=%zu\n", host.seenImg.size(), host.seenSnd.size(),
               host.missing.size(), host.failed.size());
        int k = 0;
        for (auto& m : host.missing)
            if (k++ < 15) printf("  MISSING %s\n", m.c_str());
        k = 0;
        for (auto& m : host.failed)
            if (k++ < 15) printf("  FAILED  %s\n", m.c_str());
        return 0;
    }
    return 2;
}
