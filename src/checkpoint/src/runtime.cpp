// Same-session x86 checkpoints. Memory arena / PE section discovery is adapted
// from GTA Savestates and pointlessdevice; suspended CONTEXT/stack handling follows Hourglass.
#include "timetransle/checkpoint/runtime.hpp"
#include "arena.h"
#include "graphics.hpp"
#include "audio.hpp"
#include <atomic>
#include <memory>
#include <vector>
#include <cstring>
#include <algorithm>
#include "timetransle/common/diagnostics.hpp"
using timetransle::protocol::ErrorCode;
namespace {
struct Region { uint8_t* address{}; std::vector<uint8_t> data; };
struct Snapshot {
 std::vector<uint8_t> arena,stack;
 std::vector<Region> sections;
 CONTEXT context{};
 void* seh{};
 uint8_t* stack_base{};
 int64_t virtual_qpc{};
 d3d::Capture* graphics{};
 checkpoint_audio::Capture* audio{};
};
std::unique_ptr<Snapshot> slot,candidate,retired;
timetransle::clock::VirtualClock* virtual_clock{};
std::atomic<bool> enabled{},outside_arena{},gate{},park_entered{},dispose_requested{};
std::atomic<unsigned> operation{},phase{};
std::atomic<ErrorCode> last_error{ErrorCode::ok};
HANDLE ready_event{},done_event{},render_thread{};
NT_TIB* render_tib{};
DWORD render_id{};
std::atomic<ErrorCode> render_error{ErrorCode::ok};
bool skip_restore_present{},waiting_redraw{};
decltype(&HeapAlloc) original_alloc{};
decltype(&HeapFree) original_free{};
decltype(&HeapReAlloc) original_realloc{};
decltype(&HeapSize) original_size{};
void dispose(std::unique_ptr<Snapshot>& snapshot) {
 if(!snapshot) return;
 d3d::Release(snapshot->graphics); checkpoint_audio::release(snapshot->audio); snapshot.reset();
}
void capture_failed_allocation() { outside_arena.store(true); }
LPVOID WINAPI heap_alloc(HANDLE heap,DWORD flags,SIZE_T size) {
 if(void* p=arena::Alloc(size,(flags&HEAP_ZERO_MEMORY)!=0)) return p;
 capture_failed_allocation(); return original_alloc(heap,flags,size);
}
BOOL WINAPI heap_free(HANDLE heap,DWORD flags,LPVOID p) {
 if(!p) return TRUE;
 return arena::Free(p)?TRUE:original_free(heap,flags,p);
}
LPVOID WINAPI heap_realloc(HANDLE heap,DWORD flags,LPVOID p,SIZE_T n) {
 if(arena::Contains(p)) {
  auto q=arena::Realloc(p,n,(flags&HEAP_REALLOC_IN_PLACE_ONLY)!=0,(flags&HEAP_ZERO_MEMORY)!=0);
  if(!q) capture_failed_allocation();
  return q;
 }
 capture_failed_allocation(); return original_realloc(heap,flags,p,n);
}
SIZE_T WINAPI heap_size(HANDLE heap,DWORD flags,LPCVOID p) {
 return arena::Contains(p)?arena::Size(p):original_size(heap,flags,p);
}
std::vector<Region> image_sections() {
 std::vector<Region> regions;
 auto base=(uint8_t*)GetModuleHandleW(nullptr);
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 auto section=IMAGE_FIRST_SECTION(nt);
 for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i) {
  if((section[i].Characteristics&IMAGE_SCN_MEM_WRITE)&&section[i].Misc.VirtualSize) {
   auto cursor=base+section[i].VirtualAddress;
   const auto end=cursor+section[i].Misc.VirtualSize;
   while(cursor<end) {
    MEMORY_BASIC_INFORMATION page{};
    if(!VirtualQuery(cursor,&page,sizeof(page))) break;
    const auto next=(std::min)(end,(uint8_t*)page.BaseAddress+page.RegionSize);
    const DWORD writable=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
    // The static CRT changes .fptable to read-only during startup. PE flags alone
    // are insufficient: only capture pages the application can actually modify.
    if(page.State==MEM_COMMIT&&(page.Protect&writable)&&!(page.Protect&PAGE_GUARD)) {
     Region r; r.address=cursor; r.data.assign(cursor,next); regions.push_back(std::move(r));
    }
    cursor=next;
   }
  }
 }
 return regions;
}
__declspec(noinline) void park_render() {
 SetEvent(ready_event); park_entered.store(true,std::memory_order_release);
 while(gate.load(std::memory_order_acquire)) _mm_pause();
}
void device_reset() {
 dispose(candidate); dispose(retired); dispose(slot);
 last_error.store(ErrorCode::checkpoint_device_reset);
}
__declspec(noinline) void prepare_render() {
 render_error=ErrorCode::ok;
 render_id=GetCurrentThreadId();
 if(!render_thread) render_thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,FALSE,render_id);
 if(!render_thread) { render_error=ErrorCode::checkpoint_capture_failed; return; }
 render_tib=(NT_TIB*)NtCurrentTeb();
 if(!d3d::DeviceUsable()) { render_error=ErrorCode::checkpoint_unavailable; return; }
 if(operation.load()==1) {
  dispose(candidate); candidate=std::make_unique<Snapshot>();
  candidate->graphics=d3d::Save();
  if(!candidate->graphics) { render_error=ErrorCode::checkpoint_capture_failed; return; }
  candidate->audio=checkpoint_audio::save();
  if(!candidate->audio) { render_error=ErrorCode::checkpoint_capture_failed; return; }
 } else {
  if(!slot||slot->graphics->generation!=d3d::Generation()) { render_error=ErrorCode::checkpoint_empty; return; }
  dispose(candidate); candidate=std::make_unique<Snapshot>();
  candidate->audio=checkpoint_audio::save();
  if(!candidate->audio) { render_error=ErrorCode::checkpoint_restore_failed; return; }
  if(!d3d::Load(slot->graphics)) render_error=ErrorCode::checkpoint_restore_failed;
  timetransle::common::write_diagnostic("checkpoint", "restoreGraphics", render_error==ErrorCode::ok?"ready":"failed");
 }
}
__declspec(noinline) bool finish_render() {
 if(render_error!=ErrorCode::ok) {
  if(candidate&&candidate->audio) checkpoint_audio::restore(candidate->audio);
  dispose(candidate); phase.store(0); SetEvent(done_event); return false;
 }
 if(operation.load()==2) {
  timetransle::common::write_diagnostic("checkpoint", "restoreExecution", "ready");
  if(!checkpoint_audio::restore(slot->audio)) render_error=ErrorCode::checkpoint_restore_failed;
  dispose(candidate);
  skip_restore_present=true; waiting_redraw=true;
 } else {
  if(slot&&slot->audio) checkpoint_audio::restore(slot->audio);
 }
 dispose(retired);
 phase.store(3);
 if(skip_restore_present) { skip_restore_present=false; return true; }
 return false;
}
}
namespace timetransle::checkpoint {
bool initialize(clock::VirtualClock* clock) noexcept {
 virtual_clock=clock;
 if(!arena::Init(256u<<20,0)) return false;
 ready_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 done_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 auto exe=GetModuleHandleW(nullptr);
 original_alloc=(decltype(original_alloc))PatchIat(exe,"kernel32.dll","HeapAlloc",(void*)&heap_alloc);
 original_free=(decltype(original_free))PatchIat(exe,"kernel32.dll","HeapFree",(void*)&heap_free);
 original_realloc=(decltype(original_realloc))PatchIat(exe,"kernel32.dll","HeapReAlloc",(void*)&heap_realloc);
 original_size=(decltype(original_size))PatchIat(exe,"kernel32.dll","HeapSize",(void*)&heap_size);
 d3d::onDeviceReset=&device_reset;
 bool ok=original_alloc&&original_free&&original_realloc&&original_size&&ready_event&&done_event&&d3d::InstallHooks();
 checkpoint_audio::initialize();
 enabled.store(ok); return ok;
}
ipc::CheckpointPayload state() noexcept {
 const bool outside=outside_arena.load();
 return {enabled.load()&&!outside,slot!=nullptr,(uint32_t)operation.load(),
     outside?ErrorCode::checkpoint_allocation_outside_arena:last_error.load()};
}
void clear() noexcept { enabled.store(false); dispose_requested.store(true); }
protocol::ErrorCode execute(bool load) noexcept {
 if(outside_arena.load()) return ErrorCode::checkpoint_allocation_outside_arena;
 if(!enabled.load()) return ErrorCode::checkpoint_unavailable;
 if(load&&!slot) return ErrorCode::checkpoint_empty;
 if(!d3d::Device()) return ErrorCode::checkpoint_unavailable;
 unsigned idle=0;
 if(!operation.compare_exchange_strong(idle,load?2u:1u)) return ErrorCode::checkpoint_busy;
 auto previous=virtual_clock->state();
 (void)virtual_clock->set_paused(true);
 ResetEvent(ready_event); ResetEvent(done_event);
 gate.store(true); park_entered.store(false); phase.store(1);
 auto error=ErrorCode::ok;
 if(WaitForSingleObject(ready_event,5000)!=WAIT_OBJECT_0) {
  phase.store(0); render_error=ErrorCode::timeout; gate.store(false); error=ErrorCode::timeout;
 } else {
  while(!park_entered.load(std::memory_order_acquire)) _mm_pause();
  if(render_error==ErrorCode::ok&&render_thread&&SuspendThread(render_thread)!=DWORD(-1)) {
   if(!load) {
    candidate->context.ContextFlags=CONTEXT_FULL|CONTEXT_FLOATING_POINT|CONTEXT_EXTENDED_REGISTERS;
    if(!GetThreadContext(render_thread,&candidate->context)) error=ErrorCode::checkpoint_capture_failed;
    else {
     candidate->stack_base=(uint8_t*)render_tib->StackBase;
     candidate->seh=render_tib->ExceptionList;
     auto sp=(uint8_t*)(uintptr_t)candidate->context.Esp;
     candidate->stack.assign(sp,candidate->stack_base);
     candidate->sections=image_sections();
     candidate->arena.assign(arena::Base(),arena::Base()+arena::UsedEnd());
     candidate->virtual_qpc=virtual_clock->read_qpc();
     retired=std::move(slot); slot=std::move(candidate);
    }
   } else {
    if(!arena::CommitAtLeast(slot->arena.size())) error=ErrorCode::checkpoint_restore_failed;
    else {
     memcpy(arena::Base(),slot->arena.data(),slot->arena.size());
     timetransle::common::write_diagnostic("checkpoint", "restoreArena", "ready");
     for(auto& r:slot->sections) memcpy(r.address,r.data.data(),r.data.size());
     timetransle::common::write_diagnostic("checkpoint", "restoreImage", "ready");
     auto sp=(uint8_t*)(uintptr_t)slot->context.Esp;
     memcpy(sp,slot->stack.data(),slot->stack.size());
     render_tib->ExceptionList=(EXCEPTION_REGISTRATION_RECORD*)slot->seh;
     if(!SetThreadContext(render_thread,&slot->context)) error=ErrorCode::checkpoint_restore_failed;
     (void)virtual_clock->restore_qpc(slot->virtual_qpc,previous.configured_speed,previous.paused);
     timetransle::common::write_diagnostic("checkpoint", "restoreMemory", "ready");
    }
   }
   render_error=error;
   if(!load||error!=ErrorCode::ok) (void)virtual_clock->set_paused(previous.paused);
   gate.store(false,std::memory_order_release); ResumeThread(render_thread);
  } else {
   error=render_error==ErrorCode::ok?ErrorCode::checkpoint_capture_failed:render_error.load();
   render_error=error; (void)virtual_clock->set_paused(previous.paused); gate.store(false);
  }
  if(WaitForSingleObject(done_event,5000)!=WAIT_OBJECT_0) error=ErrorCode::timeout;
  else if(render_error!=ErrorCode::ok) error=render_error;
 }
 if(error!=ErrorCode::ok) (void)virtual_clock->set_paused(previous.paused);
 operation.store(0); phase.store(0); last_error.store(error);
 return error;
}
}
bool checkpoint_before_present() {
 if(dispose_requested.exchange(false)) { dispose(candidate); dispose(retired); dispose(slot); }
 if(phase.load()==1) { phase.store(2); prepare_render(); park_render(); return finish_render(); }
 return false;
}
void checkpoint_after_present(HRESULT hr) {
 if(phase.load()!=3) return;
 if(FAILED(hr)) render_error=ErrorCode::checkpoint_restore_failed;
 waiting_redraw=false; phase.store(0); SetEvent(done_event);
}

