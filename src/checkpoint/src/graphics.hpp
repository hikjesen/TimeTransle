#pragma once
#include "support.hpp"
#include <d3d9.h>
#include <vector>
#include <unordered_map>
namespace d3d {
enum Kind : uint8_t { K_Texture, K_VolumeTexture, K_CubeTexture, K_VertexBuffer, K_IndexBuffer,
 K_RenderTarget, K_DepthStencil, K_OffscreenPlain, K_SwapChain, K_StateBlock, K_VertexDecl,
 K_VertexShader, K_PixelShader, K_Query, K_Count };
struct DevState;
struct Content { IUnknown* object{}; Kind kind{}; UINT level{}, row_bytes{}, rows{}; std::vector<uint8_t> bytes; };
struct Capture {
 std::vector<std::pair<IUnknown*, uint32_t>> refs;
 std::vector<std::pair<IUnknown*, uint32_t>> state_refs;
 IDirect3DStateBlock9* state{};
 DevState* dev{};
 std::vector<IUnknown*> permanent;
 uint32_t generation{};
 std::vector<Content> contents;
};
bool InstallHooks();
IDirect3DDevice9* Device();
bool DeviceUsable();
uint32_t Generation();
Capture* Save();
bool Load(const Capture*);
void Release(Capture*);
void Collect();
const char* KindName(int);
bool CaptureContents(Capture*);
bool RestoreContents(const Capture*);
extern void (*onDeviceReset)();
}

