// VN3DS - a VNDS visual novel player for the Nintendo 3DS.
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <cstdio>
#include <ctime>
#include <memory>

#include "../core/util.h"
#include "../core/vfs.h"
#include "audio.h"
#include "config.h"
#include "game.h"
#include "library.h"
#include "ui.h"

static FILE* g_log = nullptr;

static void logSink(vn::LogLevel lvl, const char* msg) {
    static const char* names[] = {"D", "I", "W", "E"};
    if (g_log) {
        fprintf(g_log, "[%s] %s\n", names[lvl], msg);
        fflush(g_log);
    }
}

static void drawLoading(ui::App& app, const std::string& what) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    gfx::collectGarbage();
    C2D_TargetClear(app.top, ui::BG);
    C2D_SceneBegin(app.top);
    ui::textCentered(app, "Loading…", gfx::TOP_W / 2, 100, ui::TEXT, 18);
    ui::textCentered(app, ui::ellipsize(app, what, 380), gfx::TOP_W / 2, 128, ui::TEXT_DIM, 12);
    C2D_TargetClear(app.bottom, ui::BG);
    C2D_SceneBegin(app.bottom);
    C3D_FrameEnd(0);
}

int main() {
    gfxInitDefault();
    romfsInit();
    osSetSpeedupEnable(true);  // New 3DS: 804 MHz + L2 cache
    APT_SetAppCpuTimeLimit(30);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();

    vn::makeDirs(APP_DIR);
    g_log = fopen(APP_DIR "/log.txt", "w");
    vn::setLogSink(logSink);
    vn::setLogLevel(vn::LOG_INFO);
    vn::Novel::setSaveRoot(APP_DIR "/saves");
    srand((unsigned)time(nullptr));
    g_config.load();

    ui::App app;
    bool isN3ds = false;
    APT_CheckNew3DS(&isN3ds);
    app.n3ds = isN3ds;
    gfxSet3D(true);
    app.top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    app.topRight = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    app.bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    app.font.init();
    if (!app.font.loadFallback(app.fallbackFont)) vn::logf(vn::LOG_ERROR, "fallback font missing");
    app.font.setSize(g_config.fontSize);

    bool audioOk = audio::init();
    Library library(app);
    if (!audioOk) library.setMessage("No sound: dump the DSP firmware (sdmc:/3ds/dspfirm.cdc)");
    std::unique_ptr<Game> game;

    ui::Input in;
    while (aptMainLoop() && !app.quit) {
        hidScanInput();
        in.down = hidKeysDown();
        in.held = hidKeysHeld();
        in.up = hidKeysUp();
        hidCircleRead(&in.circle);
        touchPosition tp;
        hidTouchRead(&tp);
        in.tapped = false;
        in.dragDY = 0;
        in.touchDown = in.down & KEY_TOUCH;
        in.touchHeld = in.held & KEY_TOUCH;
        in.touchUp = in.up & KEY_TOUCH;
        static bool moved = false;
        // A quick tap can press and release within one frame (emulators, fast taps):
        // handle down / held / up independently.
        if (in.touchDown) {
            in.touchStartX = in.lastX = tp.px;
            in.touchStartY = in.lastY = tp.py;
            moved = false;
        }
        if (in.touchHeld && !in.touchDown) {
            in.dragDY = tp.py - in.lastY;
            if (abs(tp.px - in.touchStartX) > 8 || abs(tp.py - in.touchStartY) > 8) moved = true;
            in.lastX = tp.px;
            in.lastY = tp.py;
        }
        if (in.touchUp) in.tapped = !moved;  // lastX/lastY keep the last touched position
        if (!moved) in.dragDY = 0;

        if (game) {
            game->update(in);
            if (game->finished()) {
                game.reset();
                library.setMessage("");
            }
        } else {
            vn::NovelEntry chosen;
            if (library.update(in, chosen)) {
                drawLoading(app, chosen.title);
                game.reset(new Game(app));
                std::string err;
                if (!game->open(chosen, err)) {
                    vn::logf(vn::LOG_ERROR, "%s", err.c_str());
                    library.setMessage(err);
                    game.reset();
                }
            }
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        gfx::collectGarbage();
        float slider = osGet3DSliderState();
        float depth = slider * g_config.depth3D * 3.0f;  // max parallax in pixels
        C2D_TargetClear(app.top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(app.top);
        if (game) game->drawTop(-depth);
        else library.drawTop();
        if (slider > 0.0f) {
            C2D_TargetClear(app.topRight, C2D_Color32(0, 0, 0, 255));
            C2D_SceneBegin(app.topRight);
            if (game) game->drawTop(depth);
            else library.drawTop();
        }
        C2D_TargetClear(app.bottom, ui::BG);
        C2D_SceneBegin(app.bottom);
        if (game) game->drawBottom();
        else library.drawBottom();
        C3D_FrameEnd(0);
    }

    game.reset();
    audio::shutdown();
    app.font.shutdown();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    if (g_log) fclose(g_log);
    return 0;
}
