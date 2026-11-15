#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstddef>
inline void Log(const char*, ...) {}
inline bool InGameImage(void*) { return true; }
inline void Unprotect(uintptr_t p, size_t n) { DWORD old; VirtualProtect((void*)p,n,PAGE_EXECUTE_READWRITE,&old); }
void* PatchIat(HMODULE module,const char* dll,const char* name,void* replacement);
void* PatchDelayIat(HMODULE module,const char* dll,const char* name,void* replacement);
bool checkpoint_before_present();
void checkpoint_after_present(HRESULT result);
enum {
 D3D_CreateDevice=16, DEV_CreateAdditionalSwapChain=13, DEV_Reset=16, DEV_Present=17,
 DEV_CreateTexture=23, DEV_CreateVolumeTexture=24, DEV_CreateCubeTexture=25,
 DEV_CreateVertexBuffer=26, DEV_CreateIndexBuffer=27, DEV_CreateRenderTarget=28,
 DEV_CreateDepthStencilSurface=29, DEV_CreateOffscreenPlainSurface=36,
 DEV_CreateStateBlock=59, DEV_EndStateBlock=61, DEV_CreateVertexDeclaration=86,
 DEV_CreateVertexShader=91, DEV_CreatePixelShader=106, DEV_CreateQuery=118
};

