#pragma once
#include "support.hpp"
#include <mmsystem.h>
#include <dsound.h>
#include <vector>
namespace checkpoint_audio {
struct BufferState {
 IDirectSoundBuffer* object{}; ULONG references{}; DWORD status{},position{},play_flags{},frequency{};
 LONG volume{},pan{}; std::vector<uint8_t> pcm;
};
struct Capture { std::vector<BufferState> buffers; };
bool initialize();
Capture* save();
bool restore(const Capture*);
void release(Capture*);
void stop_all();
void collect();
}

