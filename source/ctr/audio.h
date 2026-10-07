// Streaming audio on NDSP. Decoding runs on a separate thread (on the system core
// when available); the main thread only opens the stream and posts a request.
#pragma once
#include <3ds.h>

#include <memory>
#include <string>

#include "../core/vfs.h"

namespace audio {

// Slots 0..2 are music (BGM channels), 3..7 are sound (3 = VNDS sound / voice,
// 4..7 = Higurashi sound effect channels).
enum { MUSIC = 0, SOUND = 3, NUM_SLOTS = 8 };

bool init();  // false if NDSP is unavailable (no dspfirm.cdc) - the app keeps working muted
void shutdown();
bool available();
// loops: -1 = forever, N = play N times. gain multiplies the category volume.
void play(int slot, std::unique_ptr<vn::Stream> stream, const std::string& name, int loops, float gain = 1.0f);
void stop(int slot, int fadeMs = 0);
void stopAll();
bool isPlaying(int slot);
void setCategoryVolumes(float music, float sound);  // 0..1
std::string lastError();

}  // namespace audio
