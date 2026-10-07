#include "config.h"

#include <algorithm>

#include "../core/util.h"

Config g_config;

static int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

void Config::load() {
    vn::Ini ini;
    if (!ini.load(APP_DIR "/config.ini")) return;
    textSpeed = clampi(ini.getInt("textspeed", textSpeed), 0, 5);
    musicVolume = clampi(ini.getInt("musicvolume", musicVolume), 0, 100);
    soundVolume = clampi(ini.getInt("soundvolume", soundVolume), 0, 100);
    fontSize = clampi(ini.getInt("fontsize", fontSize), 11, 22);
    autoDelay = clampi(ini.getInt("autodelay", autoDelay), 1, 9);
    depth3D = clampi(ini.getInt("depth3d", depth3D), 0, 3);
    fitMode = clampi(ini.getInt("fitmode", fitMode), 0, 2);
    autoSave = ini.getInt("autosave", 1) != 0;
    resumeLast = ini.getInt("resumelast", 1) != 0;
    screenLayout = clampi(ini.getInt("layout", screenLayout), 0, 3);
    novelDirs.clear();
    for (auto& kv : ini.items)
        if (kv.first == "noveldir" && !kv.second.empty()) novelDirs.push_back(kv.second);
}

void Config::save() const {
    std::string s = "# VN3DS settings\n";
    s += "textspeed=" + std::to_string(textSpeed) + "\n";
    s += "musicvolume=" + std::to_string(musicVolume) + "\n";
    s += "soundvolume=" + std::to_string(soundVolume) + "\n";
    s += "fontsize=" + std::to_string(fontSize) + "\n";
    s += "autodelay=" + std::to_string(autoDelay) + "\n";
    s += "depth3d=" + std::to_string(depth3D) + "\n";
    s += "fitmode=" + std::to_string(fitMode) + "\n";
    s += std::string("autosave=") + (autoSave ? "1" : "0") + "\n";
    s += std::string("resumelast=") + (resumeLast ? "1" : "0") + "\n";
    s += "layout=" + std::to_string(screenLayout) + "\n";
    s += "# extra folders to scan for novels (one per line), e.g.\n# noveldir=sdmc:/my/novels\n";
    for (auto& d : novelDirs) s += "noveldir=" + d + "\n";
    vn::makeDirs(APP_DIR);
    vn::writeWholeFile(APP_DIR "/config.ini", s.data(), s.size());
}

std::vector<std::string> Config::allNovelDirs() const {
    std::vector<std::string> d = {
        "sdmc:/vnds/novels", "sdmc:/3ds/vnds/novels", APP_DIR "/novels", "sdmc:/3ds/save/novels",
        "sdmc:/VNs",         "sdmc:/vnds",
    };
    for (auto& x : novelDirs) d.push_back(x);
    return d;
}
