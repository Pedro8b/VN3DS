// Novel list: scans the usual VNDS folders and shows icon/thumbnail/title.
#pragma once
#include <map>
#include <vector>

#include "../core/vfs.h"
#include "ui.h"

class Library {
public:
    explicit Library(ui::App& app) : app_(app) {}
    void refresh();
    // Returns true when the user picked a novel (stored in 'chosen').
    bool update(const ui::Input& in, vn::NovelEntry& chosen);
    void drawTop();
    void drawBottom();
    void setMessage(const std::string& m) { message_ = m; }

private:
    gfx::TexPtr loadThumb(const vn::NovelEntry& e, const char* name, int maxW, int maxH);
    ui::App& app_;
    std::vector<vn::NovelEntry> entries_;
    std::map<std::string, gfx::TexPtr> icons_;
    gfx::TexPtr thumb_;
    int thumbFor_ = -1;
    int sel_ = 0;
    float scroll_ = 0;
    bool scanned_ = false;
    std::string message_;
};
