#pragma once
#include <string>
#include <vector>

#define APP_DIR "sdmc:/3ds/VN3DS"

struct Config {
    int textSpeed = 3;     // 0 (slow) .. 5 (instant)
    int musicVolume = 70;  // 0..100
    int soundVolume = 100;
    int fontSize = 15;     // px, 11..22
    int autoDelay = 3;     // 1..9, extra wait in auto mode
    int depth3D = 2;       // 0 (off) .. 3, stereoscopic depth
    int fitMode = 0;       // 0 fit (bars), 1 zoom (crop), 2 stretch
    bool autoSave = true;    // periodic + on exit autosave slot
    bool resumeLast = true;  // load the newest save when a novel is opened
    int screenLayout = 0;    // 0 picture top / text bottom, 1 swapped, 2 one screen (top), 3 one screen (bottom)
    std::vector<std::string> novelDirs;

    void load();
    void save() const;
    std::vector<std::string> allNovelDirs() const;
};

extern Config g_config;
