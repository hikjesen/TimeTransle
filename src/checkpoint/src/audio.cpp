// Copyright (C) 2011 nitsuja and contributors.
// Hourglass is licensed under GPL v2. Full notice: third_party/checkpoint/Hourglass-COPYING.txt.
// Modified for TimeTransle, 2026-11-15: physical buffer tracking and same-session snapshots.
// DirectSound buffer tracking/stop/cursor restoration adapted from Hourglass's
// MyDirectSoundBuffer (GPLv2). Physical buffers remain outside application snapshots.
#include "audio.hpp"
#include <unordered_map>
#include <memory>
#include <cstring>
#include <string>
#include "timetransle/common/diagnostics.hpp"
namespace checkpoint_audio {
namespace {
struct Tracked { unsigned snapshots{}; DWORD flags{}; };
std::unordered_map<IDirectSoundBuffer*,Tracked> buffers;
std::unordered_map<void**,void*> create_original,duplicate_original,release_original,play_original;
HRESULT (WINAPI* real_create8)(LPCGUID,LPDIRECTSOUND8*,LPUNKNOWN);
HRESULT (WINAPI* real_create)(LPCGUID,LPDIRECTSOUND*,LPUNKNOWN);
CRITICAL_SECTION cs;
thread_local bool internal=false;
void replace(void** vt,int slot,void* hook,std::unordered_map<void**,void*>& originals) {
 if(vt[slot]==hook) return;
 originals[vt]=vt[slot]; DWORD old;
 VirtualProtect(vt+slot,sizeof(void*),PAGE_READWRITE,&old);
 vt[slot]=hook; DWORD unused; VirtualProtect(vt+slot,sizeof(void*),old,&unused);
}
ULONG raw_count(IDirectSoundBuffer* b) { auto n=b->AddRef(); b->Release(); return n-1; }
ULONG STDMETHODCALLTYPE on_release(IDirectSoundBuffer* b) {
 auto f=(ULONG(STDMETHODCALLTYPE*)(IDirectSoundBuffer*))release_original[*(void***)b];
 ULONG n=f(b);
 if(internal) return n;
 return buffers.count(b)&&n?n-1:n;
}
HRESULT STDMETHODCALLTYPE on_play(IDirectSoundBuffer* b,DWORD a,DWORD c,DWORD flags) {
 auto it=buffers.find(b); if(it!=buffers.end()) it->second.flags=flags;
 return ((HRESULT(STDMETHODCALLTYPE*)(IDirectSoundBuffer*,DWORD,DWORD,DWORD))play_original[*(void***)b])(b,a,c,flags);
}
void track(IDirectSoundBuffer* b) {
 DSBCAPS caps{sizeof(caps)}; if(!b||FAILED(b->GetCaps(&caps))||(caps.dwFlags&DSBCAPS_PRIMARYBUFFER)) return;
 EnterCriticalSection(&cs);
 if(!buffers.count(b)) {
  b->AddRef(); buffers.emplace(b,Tracked{});
  auto vt=*(void***)b;
  replace(vt,2,(void*)&on_release,release_original);
  replace(vt,12,(void*)&on_play,play_original);
 }
 LeaveCriticalSection(&cs);
}
HRESULT STDMETHODCALLTYPE on_buffer(IDirectSound* d,LPCDSBUFFERDESC desc,LPDIRECTSOUNDBUFFER* out,LPUNKNOWN outer) {
 auto hr=((HRESULT(STDMETHODCALLTYPE*)(IDirectSound*,LPCDSBUFFERDESC,LPDIRECTSOUNDBUFFER*,LPUNKNOWN))create_original[*(void***)d])(d,desc,out,outer);
 if(SUCCEEDED(hr)&&out) track(*out); return hr;
}
HRESULT STDMETHODCALLTYPE on_duplicate(IDirectSound* d,LPDIRECTSOUNDBUFFER source,LPDIRECTSOUNDBUFFER* out) {
 auto hr=((HRESULT(STDMETHODCALLTYPE*)(IDirectSound*,LPDIRECTSOUNDBUFFER,LPDIRECTSOUNDBUFFER*))duplicate_original[*(void***)d])(d,source,out);
 if(SUCCEEDED(hr)&&out) track(*out); return hr;
}
void device(IDirectSound* d) {
 if(!d) return; auto vt=*(void***)d;
 replace(vt,3,(void*)&on_buffer,create_original); replace(vt,5,(void*)&on_duplicate,duplicate_original);
}
HRESULT WINAPI create8(LPCGUID g,LPDIRECTSOUND8* out,LPUNKNOWN outer) {
 auto hr=real_create8(g,out,outer); if(SUCCEEDED(hr)&&out) device(*out); return hr;
}
HRESULT WINAPI create(LPCGUID g,LPDIRECTSOUND* out,LPUNKNOWN outer) {
 auto hr=real_create(g,out,outer); if(SUCCEEDED(hr)&&out) device(*out); return hr;
}
bool copy_pcm(IDirectSoundBuffer* b,std::vector<uint8_t>& pcm,bool write) {
 void *p1{},*p2{}; DWORD n1{},n2{};
 if(FAILED(b->Lock(0,0,&p1,&n1,&p2,&n2,DSBLOCK_ENTIREBUFFER))) return false;
 if(write) {
  if(pcm.size()!=size_t(n1)+n2) { b->Unlock(p1,n1,p2,n2); return false; }
  memcpy(p1,pcm.data(),n1); if(n2) memcpy(p2,pcm.data()+n1,n2);
 } else {
  pcm.resize(size_t(n1)+n2); memcpy(pcm.data(),p1,n1); if(n2) memcpy(pcm.data()+n1,p2,n2);
 }
 b->Unlock(p1,n1,p2,n2); return true;
}
}
bool initialize() {
 InitializeCriticalSection(&cs);
 auto exe=GetModuleHandleW(nullptr);
 real_create8=(decltype(real_create8))PatchIat(exe,"dsound.dll","DirectSoundCreate8",(void*)&create8);
 real_create=(decltype(real_create))PatchIat(exe,"dsound.dll","DirectSoundCreate",(void*)&create);
 return real_create8||real_create;
}
void stop_all() { for(auto& [b,t]:buffers) { (void)t; b->Stop(); } }
Capture* save() {
 auto c=std::make_unique<Capture>(); internal=true;
 for(auto& [b,t]:buffers) {
  ULONG refs=raw_count(b)-1; if(!refs) continue;
  BufferState s{}; s.object=b; s.references=refs; s.play_flags=t.flags;
  b->GetStatus(&s.status); b->GetVolume(&s.volume); b->GetPan(&s.pan); b->GetFrequency(&s.frequency);
  b->Stop(); b->GetCurrentPosition(&s.position,nullptr);
  timetransle::common::write_diagnostic("checkpoint", "audioSaveCursor", std::to_string(s.position));
  if(!copy_pcm(b,s.pcm,false)) {
   if(s.status&DSBSTATUS_PLAYING) b->Play(0,0,s.play_flags);
   restore(c.get()); release(c.release()); internal=false; return nullptr;
  }
  ++t.snapshots; c->buffers.push_back(std::move(s));
 }
 internal=false; return c.release();
}
bool restore(const Capture* c) {
 internal=true; stop_all();
 for(auto& [b,t]:buffers) {
  (void)t; ULONG want=0;
  for(auto& s:c->buffers) if(s.object==b) { want=s.references; break; }
  ULONG current=raw_count(b)-1;
  while(current<want) { b->AddRef(); ++current; }
  while(current>want) { b->Release(); --current; }
 }
 bool ok=true;
 for(auto& saved:c->buffers) {
  auto& s=const_cast<BufferState&>(saved);
  if(!copy_pcm(s.object,s.pcm,true)) { ok=false; break; }
  if(FAILED(s.object->SetVolume(s.volume)) || FAILED(s.object->SetPan(s.pan)) ||
     FAILED(s.object->SetFrequency(s.frequency)) || FAILED(s.object->SetCurrentPosition(s.position))) { ok=false; break; }
  DWORD actual{};
  if(FAILED(s.object->GetCurrentPosition(&actual,nullptr))) { ok=false; break; }
  timetransle::common::write_diagnostic("checkpoint", "audioRestoreCursor",
      std::to_string(s.position)+" -> "+std::to_string(actual));
  if((s.status&DSBSTATUS_PLAYING) && FAILED(s.object->Play(0,0,s.play_flags))) { ok=false; break; }
 }
 internal=false; return ok;
}
void collect() {
 internal=true;
 for(auto i=buffers.begin();i!=buffers.end();) {
  if(!i->second.snapshots&&raw_count(i->first)<=1) {
   auto b=i->first; i=buffers.erase(i); b->Release();
  } else ++i;
 }
 internal=false;
}
void release(Capture* c) {
 if(!c) return;
 for(auto& s:c->buffers) { auto i=buffers.find(s.object); if(i!=buffers.end()&&i->second.snapshots) --i->second.snapshots; }
 delete c; collect();
}
}

