#include "support.hpp"
#include <cstring>
#include <delayimp.h>
namespace {
void* swap(void** slot,void* replacement) {
 void* old=*slot; DWORD protect;
 if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protect)) return nullptr;
 *slot=replacement; DWORD ignored; VirtualProtect(slot,sizeof(void*),protect,&ignored);
 return old;
}
}
void* PatchIat(HMODULE module,const char* dll,const char* name,void* replacement) {
 auto base=(uint8_t*)module;
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
 if(!dir.VirtualAddress) return nullptr;
 for(auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);imp->Name;++imp) {
  if(_stricmp((char*)base+imp->Name,dll)) continue;
  auto iat=(IMAGE_THUNK_DATA*)(base+imp->FirstThunk);
  auto names=imp->OriginalFirstThunk?(IMAGE_THUNK_DATA*)(base+imp->OriginalFirstThunk):nullptr;
  void* actual=(void*)GetProcAddress(GetModuleHandleA(dll),name);
  for(size_t i=0;iat[i].u1.Function;++i) {
   bool match=names&&!IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)?
    !strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base+names[i].u1.AddressOfData))->Name,name):
    (void*)iat[i].u1.Function==actual;
   if(match) return swap((void**)&iat[i].u1.Function,replacement);
  }
 }
 return nullptr;
}
void* PatchDelayIat(HMODULE module,const char* dll,const char* name,void* replacement) {
 auto base=(uint8_t*)module;
 auto nt=(IMAGE_NT_HEADERS*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
 if(!dir.VirtualAddress) return nullptr;
 for(auto d=(ImgDelayDescr*)(base+dir.VirtualAddress);d->rvaDLLName;++d) {
  if(_stricmp((char*)base+d->rvaDLLName,dll)) continue;
  auto iat=(IMAGE_THUNK_DATA*)(base+d->rvaIAT);
  auto names=(IMAGE_THUNK_DATA*)(base+d->rvaINT);
  for(size_t i=0;names[i].u1.AddressOfData;++i)
   if(!IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)&&
      !strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base+names[i].u1.AddressOfData))->Name,name))
    return swap((void**)&iat[i].u1.Function,replacement);
 }
 return nullptr;
}

