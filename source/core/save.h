// Save files in the VNDS XML format (save/saveNN.sav, save/global.sav), so saves
// are interchangeable with VNDS on the DS. Extra fields we need for exact resume
// (<line>, <text>) are ignored by other VNDS versions.
#pragma once
#include <string>

#include "script.h"

namespace vn {

bool writeSave(const std::string& path, const SaveState& s);
bool readSave(const std::string& path, SaveState& s);
bool writeGlobals(const std::string& path, const VarMap& g);
bool readGlobals(const std::string& path, VarMap& g);
std::string nowString();  // "HH:MM YYYY/MM/DD", like VNDS

}  // namespace vn
