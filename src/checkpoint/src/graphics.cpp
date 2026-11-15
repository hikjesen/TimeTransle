#include "graphics.hpp"
#include "support.hpp"
#include <smmintrin.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdio>

namespace d3d {

void (*onDeviceReset)() = nullptr;
bool releaseCheck = false;
bool forceWindowed = false;
int windowX = 40, windowY = 40;

namespace {

HWND g_hwnd = nullptr;

void MakeWindowed(D3DPRESENT_PARAMETERS* pp, HWND hwnd) {
    if (!pp) return;
    if (!hwnd) hwnd = pp->hDeviceWindow ? pp->hDeviceWindow : g_hwnd;
    g_hwnd = hwnd;
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    if (pp->BackBufferFormat == D3DFMT_UNKNOWN || pp->BackBufferFormat == D3DFMT_A8R8G8B8)
        pp->BackBufferFormat = D3DFMT_X8R8G8B8;
    if (hwnd && pp->BackBufferWidth && pp->BackBufferHeight) {
        LONG style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowLongA(hwnd, GWL_EXSTYLE, 0);
        RECT r = {0, 0, (LONG)pp->BackBufferWidth, (LONG)pp->BackBufferHeight};
        AdjustWindowRect(&r, style, FALSE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, windowX, windowY, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

// IDirect3D9 / IDirect3DDevice9 vtable slots in d3d9.h order.
enum : int {
    D3D_CreateDevice = 16,
    DEV_CreateAdditionalSwapChain = 13, DEV_Reset = 16, DEV_Present = 17,
    DEV_CreateTexture = 23, DEV_CreateVolumeTexture = 24, DEV_CreateCubeTexture = 25,
    DEV_CreateVertexBuffer = 26, DEV_CreateIndexBuffer = 27, DEV_CreateRenderTarget = 28,
    DEV_CreateDepthStencilSurface = 29, DEV_CreateOffscreenPlainSurface = 36,
    DEV_CreateStateBlock = 59, DEV_EndStateBlock = 61, DEV_CreateVertexDeclaration = 86,
    DEV_CreateVertexShader = 91, DEV_CreatePixelShader = 106, DEV_CreateQuery = 118,
};

struct Obj {
    Kind     kind;
    bool     defaultPool;
    bool     permanent;  // never collected (see PinForever)
    uint32_t snapRefs;   // captures that reference this object
    uint32_t flags;      // render target: lockable; depth stencil: discard
    uint32_t stateRefs{}; // references owned by our state blocks, excluded from application counts
};

std::unordered_map<IUnknown*, Obj> g_objs;
CRITICAL_SECTION g_cs;
IDirect3DDevice9* g_dev = nullptr;
uint32_t g_generation = 1;
void** g_devVt = nullptr;
void* g_orig[128];

template <typename F> F Orig(int slot) { return (F)g_orig[slot]; }

void HookSlot(void** vt, int slot, void* fn) {
    if (vt[slot] == fn) return;
    g_orig[slot] = vt[slot];
    Unprotect((uintptr_t)&vt[slot], 4);
    vt[slot] = fn;
}

void HookRelease(IUnknown* obj);
void HookTextureVt(IUnknown* t);
void HookSurfaceVt(IUnknown* sf);

void TrackObj(IUnknown* obj, Kind kind, bool defaultPool, bool permanent, uint32_t flags = 0) {
    EnterCriticalSection(&g_cs);
    auto it = g_objs.find(obj);
    if (it == g_objs.end()) {
        obj->AddRef();   // the pin: the object outlives the game's last Release until Collect() sees it unused
        g_objs.emplace(obj, Obj{kind, defaultPool, permanent, 0, flags});
        HookRelease(obj);
        if (kind == K_Texture) HookTextureVt(obj);
        else if (kind == K_RenderTarget || kind == K_DepthStencil || kind == K_OffscreenPlain) HookSurfaceVt(obj);
    }
    LeaveCriticalSection(&g_cs);
}

void TrackIfGame(void* ret, IUnknown* obj, Kind kind, bool defaultPool, uint32_t flags = 0) {
    if (obj && InGameImage(ret)) TrackObj(obj, kind, defaultPool, false, flags);
}

// RenderWare caches vertex declarations by raw pointer: the cache itself holds no reference (each user gets one,
// AddRef on a cache hit), so a declaration must never be destroyed even when no user is left. Shaders are created
// once at startup. Their references are still reconciled on load like any other object's.
void PinForever(void* ret, IUnknown* obj, Kind kind) {
    if (obj && InGameImage(ret)) TrackObj(obj, kind, false, true);
}

// Lab diagnostic: catch a game Release that consumes our pin (an over-release by somebody).
int g_ourRelease = 0;
std::unordered_map<void**, ULONG(STDMETHODCALLTYPE*)(IUnknown*)> g_origRelease;

ULONG STDMETHODCALLTYPE H_Release(IUnknown* self) {
    void** vt = *(void***)self;
    ULONG r = g_origRelease[vt](self);
    if (r == 0 && !g_ourRelease) {
        auto it = g_objs.find(self);
        if (it != g_objs.end()) {
            Log("OVER-RELEASE: %s object %p (vtable %p) reached 0 refs, released from %p",
                it->second.permanent ? "permanent" : "tracked", self, vt, _ReturnAddress());
            g_objs.erase(it);
        }
    }
    return r;
}

void HookRelease(IUnknown* obj) {
    if (!releaseCheck) return;
    void** vt = *(void***)obj;
    if (g_origRelease.count(vt)) return;
    g_origRelease[vt] = (ULONG(STDMETHODCALLTYPE*)(IUnknown*))vt[2];
    Unprotect((uintptr_t)&vt[2], 4);
    vt[2] = (void*)&H_Release;
}

ULONG RefCount(IUnknown* o) {   // current external reference count
    ULONG n = o->AddRef();
    o->Release();
    return n - 1;
}

// ------------------------------------------------------------------ outstanding locks
// RenderWare keeps some rasters locked across frames (the number plate charset stays locked for the whole game), with
// the locked surface and the pixel pointer stored in game memory. A savestate records such locks, so that loading it
// in another process can lock the re-created texture and point the game at the new pixels.
struct LockRec { RECT rect; bool whole; DWORD flags; void* bits; INT pitch; IUnknown* container; };
std::unordered_map<IUnknown*, LockRec> g_surfLocks;                  // locked surface -> lock
std::map<std::pair<IUnknown*, UINT>, LockRec> g_texLocks;           // (texture, level) -> lock

enum { V_SurfLock, V_SurfUnlock, V_TexLock, V_TexUnlock, V_TexSurfaceLevel, V_Count };
std::unordered_map<void**, void*> g_vtOrig[V_Count];               // hooked vtable -> original method

void HookVt(void** vt, int which, int slot, void* fn) {
    auto& m = g_vtOrig[which];
    if (m.count(vt) || vt[slot] == fn) return;
    m[vt] = vt[slot];
    Unprotect((uintptr_t)&vt[slot], 4);
    vt[slot] = fn;
}

template <typename F> F VtOrig(int which, void* obj) { return (F)g_vtOrig[which][*(void***)obj]; }

HRESULT STDMETHODCALLTYPE H_SurfLock(IDirect3DSurface9* sf, D3DLOCKED_RECT* lr, const RECT* r, DWORD fl) {
    HRESULT hr = VtOrig<decltype(&H_SurfLock)>(V_SurfLock, sf)(sf, lr, r, fl);
    if (SUCCEEDED(hr) && lr) {
        EnterCriticalSection(&g_cs);
        IUnknown* container = nullptr;
        if (g_objs.count(sf)) {
            container = sf;
        } else if (SUCCEEDED(sf->GetContainer(__uuidof(IDirect3DTexture9), (void**)&container)) && container) {
            container->Release();
            if (!g_objs.count(container)) container = nullptr;
        }
        if (container) g_surfLocks[sf] = LockRec{r ? *r : RECT{}, !r, fl, lr->pBits, lr->Pitch, container};
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE H_SurfUnlock(IDirect3DSurface9* sf) {
    EnterCriticalSection(&g_cs);
    g_surfLocks.erase(sf);
    LeaveCriticalSection(&g_cs);
    return VtOrig<decltype(&H_SurfUnlock)>(V_SurfUnlock, sf)(sf);
}

HRESULT STDMETHODCALLTYPE H_TexLock(IDirect3DTexture9* t, UINT l, D3DLOCKED_RECT* lr, const RECT* r, DWORD fl) {
    HRESULT hr = VtOrig<decltype(&H_TexLock)>(V_TexLock, t)(t, l, lr, r, fl);
    if (SUCCEEDED(hr) && lr) {
        EnterCriticalSection(&g_cs);
        g_texLocks[{t, l}] = LockRec{r ? *r : RECT{}, !r, fl, lr->pBits, lr->Pitch, t};
        // d3d9 locks the level's surface through its vtable but unlocks it directly: that record would never go
        for (auto it = g_surfLocks.begin(); it != g_surfLocks.end();)
            it = it->second.container == t && it->second.bits == lr->pBits ? g_surfLocks.erase(it) : std::next(it);
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE H_TexUnlock(IDirect3DTexture9* t, UINT l) {
    EnterCriticalSection(&g_cs);
    g_texLocks.erase({t, l});
    LeaveCriticalSection(&g_cs);
    return VtOrig<decltype(&H_TexUnlock)>(V_TexUnlock, t)(t, l);
}

void ForgetLocks(IUnknown* container) {
    for (auto it = g_surfLocks.begin(); it != g_surfLocks.end();)
        it = it->second.container == container ? g_surfLocks.erase(it) : std::next(it);
    for (auto it = g_texLocks.begin(); it != g_texLocks.end();)
        it = it->first.first == container ? g_texLocks.erase(it) : std::next(it);
}

void HookSurfaceVt(IUnknown* sf) {
    void** vt = *(void***)sf;
    HookVt(vt, V_SurfLock, 13, (void*)&H_SurfLock);
    HookVt(vt, V_SurfUnlock, 14, (void*)&H_SurfUnlock);
}

HRESULT STDMETHODCALLTYPE H_TexSurfaceLevel(IDirect3DTexture9* t, UINT l, IDirect3DSurface9** out) {
    HRESULT hr = VtOrig<decltype(&H_TexSurfaceLevel)>(V_TexSurfaceLevel, t)(t, l, out);
    if (SUCCEEDED(hr) && out && *out) HookSurfaceVt(*out);
    return hr;
}

void HookTextureVt(IUnknown* t) {
    void** vt = *(void***)t;
    HookVt(vt, V_TexSurfaceLevel, 18, (void*)&H_TexSurfaceLevel);
    HookVt(vt, V_TexLock, 19, (void*)&H_TexLock);
    HookVt(vt, V_TexUnlock, 20, (void*)&H_TexUnlock);
}

// ------------------------------------------------------------------ creation hooks
#define RET _ReturnAddress()
HRESULT STDMETHODCALLTYPE H_CreateTexture(IDirect3DDevice9* d, UINT w, UINT h, UINT lv, DWORD use, D3DFORMAT f,
                                          D3DPOOL p, IDirect3DTexture9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateTexture)>(DEV_CreateTexture)(d, w, h, lv, use, f, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_Texture, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateVolumeTexture(IDirect3DDevice9* d, UINT w, UINT h, UINT dp, UINT lv, DWORD use,
                                                D3DFORMAT f, D3DPOOL p, IDirect3DVolumeTexture9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateVolumeTexture)>(DEV_CreateVolumeTexture)(d, w, h, dp, lv, use, f, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_VolumeTexture, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateCubeTexture(IDirect3DDevice9* d, UINT e, UINT lv, DWORD use, D3DFORMAT f,
                                              D3DPOOL p, IDirect3DCubeTexture9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateCubeTexture)>(DEV_CreateCubeTexture)(d, e, lv, use, f, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_CubeTexture, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateVertexBuffer(IDirect3DDevice9* d, UINT len, DWORD use, DWORD fvf, D3DPOOL p,
                                               IDirect3DVertexBuffer9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateVertexBuffer)>(DEV_CreateVertexBuffer)(d, len, use, fvf, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_VertexBuffer, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateIndexBuffer(IDirect3DDevice9* d, UINT len, DWORD use, D3DFORMAT f, D3DPOOL p,
                                              IDirect3DIndexBuffer9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateIndexBuffer)>(DEV_CreateIndexBuffer)(d, len, use, f, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_IndexBuffer, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateRenderTarget(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f,
                                               D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL lock, IDirect3DSurface9** o,
                                               HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateRenderTarget)>(DEV_CreateRenderTarget)(d, w, h, f, ms, q, lock, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_RenderTarget, true, lock ? 1 : 0);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateDepthStencilSurface(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f,
                                                      D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL disc,
                                                      IDirect3DSurface9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateDepthStencilSurface)>(DEV_CreateDepthStencilSurface)(d, w, h, f, ms, q, disc, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_DepthStencil, true, disc ? 1 : 0);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateOffscreenPlainSurface(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f, D3DPOOL p,
                                                        IDirect3DSurface9** o, HANDLE* sh) {
    HRESULT hr = Orig<decltype(&H_CreateOffscreenPlainSurface)>(DEV_CreateOffscreenPlainSurface)(d, w, h, f, p, o, sh);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_OffscreenPlain, p == D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateAdditionalSwapChain(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp,
                                                      IDirect3DSwapChain9** o) {
    HRESULT hr = Orig<decltype(&H_CreateAdditionalSwapChain)>(DEV_CreateAdditionalSwapChain)(d, pp, o);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_SwapChain, true);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateStateBlock(IDirect3DDevice9* d, D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** o) {
    HRESULT hr = Orig<decltype(&H_CreateStateBlock)>(DEV_CreateStateBlock)(d, t, o);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_StateBlock, true);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_EndStateBlock(IDirect3DDevice9* d, IDirect3DStateBlock9** o) {
    HRESULT hr = Orig<decltype(&H_EndStateBlock)>(DEV_EndStateBlock)(d, o);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_StateBlock, true);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateVertexDeclaration(IDirect3DDevice9* d, const D3DVERTEXELEMENT9* e,
                                                    IDirect3DVertexDeclaration9** o) {
    HRESULT hr = Orig<decltype(&H_CreateVertexDeclaration)>(DEV_CreateVertexDeclaration)(d, e, o);
    if (SUCCEEDED(hr) && o) PinForever(RET, *o, K_VertexDecl);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateVertexShader(IDirect3DDevice9* d, const DWORD* fn, IDirect3DVertexShader9** o) {
    HRESULT hr = Orig<decltype(&H_CreateVertexShader)>(DEV_CreateVertexShader)(d, fn, o);
    if (SUCCEEDED(hr) && o) PinForever(RET, *o, K_VertexShader);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreatePixelShader(IDirect3DDevice9* d, const DWORD* fn, IDirect3DPixelShader9** o) {
    HRESULT hr = Orig<decltype(&H_CreatePixelShader)>(DEV_CreatePixelShader)(d, fn, o);
    if (SUCCEEDED(hr) && o) PinForever(RET, *o, K_PixelShader);
    return hr;
}
HRESULT STDMETHODCALLTYPE H_CreateQuery(IDirect3DDevice9* d, D3DQUERYTYPE t, IDirect3DQuery9** o) {
    HRESULT hr = Orig<decltype(&H_CreateQuery)>(DEV_CreateQuery)(d, t, o);
    if (SUCCEEDED(hr) && o) TrackIfGame(RET, *o, K_Query, false);
    return hr;
}
#undef RET

HRESULT STDMETHODCALLTYPE H_Present(IDirect3DDevice9* d, const RECT* s, const RECT* dst, HWND w, const RGNDATA* r) {
    if (checkpoint_before_present()) return D3D_OK;
    HRESULT hr = Orig<decltype(&H_Present)>(DEV_Present)(d, s, dst, w, r);
    checkpoint_after_present(hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE H_Reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp) {
    Log("device reset: savestates referencing GPU objects are dropped");
    if (onDeviceReset) onDeviceReset();
    Collect();
    if (forceWindowed) MakeWindowed(pp, g_hwnd);
    HRESULT hr = Orig<decltype(&H_Reset)>(DEV_Reset)(d, pp);
    g_generation++;
    Log("device reset -> 0x%08X (%u objects tracked)", (unsigned)hr, (unsigned)g_objs.size());
    return hr;
}

void HookDevice(IDirect3DDevice9* dev) {
    void** vt = *(void***)dev;
    g_devVt = vt;
    HookSlot(vt, DEV_CreateAdditionalSwapChain, (void*)&H_CreateAdditionalSwapChain);
    HookSlot(vt, DEV_Reset, (void*)&H_Reset);
    HookSlot(vt, DEV_Present, (void*)&H_Present);
    HookSlot(vt, DEV_CreateTexture, (void*)&H_CreateTexture);
    HookSlot(vt, DEV_CreateVolumeTexture, (void*)&H_CreateVolumeTexture);
    HookSlot(vt, DEV_CreateCubeTexture, (void*)&H_CreateCubeTexture);
    HookSlot(vt, DEV_CreateVertexBuffer, (void*)&H_CreateVertexBuffer);
    HookSlot(vt, DEV_CreateIndexBuffer, (void*)&H_CreateIndexBuffer);
    HookSlot(vt, DEV_CreateRenderTarget, (void*)&H_CreateRenderTarget);
    HookSlot(vt, DEV_CreateDepthStencilSurface, (void*)&H_CreateDepthStencilSurface);
    HookSlot(vt, DEV_CreateOffscreenPlainSurface, (void*)&H_CreateOffscreenPlainSurface);
    HookSlot(vt, DEV_CreateStateBlock, (void*)&H_CreateStateBlock);
    HookSlot(vt, DEV_EndStateBlock, (void*)&H_EndStateBlock);
    HookSlot(vt, DEV_CreateVertexDeclaration, (void*)&H_CreateVertexDeclaration);
    HookSlot(vt, DEV_CreateVertexShader, (void*)&H_CreateVertexShader);
    HookSlot(vt, DEV_CreatePixelShader, (void*)&H_CreatePixelShader);
    HookSlot(vt, DEV_CreateQuery, (void*)&H_CreateQuery);
}

typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
CreateDevice_t g_origCreateDevice = nullptr;
IDirect3D9* (WINAPI* g_origDirect3DCreate9)(UINT) = nullptr;
IDirect3D9* g_d3d = nullptr;

HRESULT STDMETHODCALLTYPE H_CreateDevice(IDirect3D9* d3d, UINT ad, D3DDEVTYPE t, HWND wnd, DWORD fl,
                                         D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    if (forceWindowed) MakeWindowed(pp, wnd);
    HRESULT hr = g_origCreateDevice(d3d, ad, t, wnd, fl, pp, out);
    if (SUCCEEDED(hr) && out && *out) {
        g_dev = *out;
        HookDevice(g_dev);
        Log("CreateDevice ok: device %p vtable %p (%ux%u windowed=%d)", g_dev, *(void**)g_dev,
            pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0, pp ? pp->Windowed : -1);
    } else {
        Log("CreateDevice failed 0x%08X", (unsigned)hr);
    }
    return hr;
}

IDirect3D9* WINAPI H_Direct3DCreate9(UINT sdk) {
    if (!g_origDirect3DCreate9) {   // delay-loaded import: load the same d3d9.dll the game would
        HMODULE m = LoadLibraryA("d3d9.dll");
        g_origDirect3DCreate9 = m ? (decltype(g_origDirect3DCreate9))GetProcAddress(m, "Direct3DCreate9") : nullptr;
        if (!g_origDirect3DCreate9) return nullptr;
    }
    IDirect3D9* d3d = g_origDirect3DCreate9(sdk);
    if (d3d) {
        g_d3d = d3d;
        void** vt = *(void***)d3d;
        if (vt[D3D_CreateDevice] != (void*)&H_CreateDevice) {
            g_origCreateDevice = (CreateDevice_t)vt[D3D_CreateDevice];
            Unprotect((uintptr_t)&vt[D3D_CreateDevice], 4);
            vt[D3D_CreateDevice] = (void*)&H_CreateDevice;
        }
    }
    Log("Direct3DCreate9(%u) = %p", sdk, d3d);
    return d3d;
}

// ------------------------------------------------------------------ device state as values
const uint8_t kRenderStates[] = {
    7, 8, 9, 14, 15, 16, 19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 34, 35, 36, 37, 38, 48, 52, 53, 54, 55, 56, 57, 58,
    59, 60, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 139, 140, 141, 142, 143, 145, 146, 147, 148, 151, 152,
    154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 166, 167, 168, 170, 171, 172, 173, 174, 175, 176, 178, 179, 180,
    181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 198, 199, 200, 201, 202, 203, 204, 205,
    206, 207, 208, 209};
const uint8_t kStageStates[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 26, 27, 28, 32};
const DWORD kSamplers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 257, 258, 259, 260};
const D3DTRANSFORMSTATETYPE kTransforms[] = {D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_TEXTURE0, D3DTS_TEXTURE1,
    D3DTS_TEXTURE2, D3DTS_TEXTURE3, D3DTS_TEXTURE4, D3DTS_TEXTURE5, D3DTS_TEXTURE6, D3DTS_TEXTURE7, D3DTS_WORLD};
constexpr int NRS = sizeof(kRenderStates), NTSS = sizeof(kStageStates), NSMP = 20, NXF = 11;

}  // namespace

// Everything Get* can read back, with object pointers stored as values (they are mapped when applied).
struct DevState {
    DWORD rs[NRS]; uint8_t rsOk[NRS];
    DWORD tss[8][NTSS];
    DWORD ss[NSMP][14];
    uint32_t tex[NSMP];
    struct { uint32_t vb; UINT off, stride, freq; } streams[16];
    uint32_t ib, decl, vs, ps, rt[4], ds;
    DWORD fvf;
    D3DMATRIX xf[NXF];
    D3DVIEWPORT9 vp;
    D3DMATERIAL9 mat;
    D3DLIGHT9 light[16]; uint8_t lightOk[16]; BOOL lightOn[16];
    float clip[6][4];
    UINT nvsF, npsF;
    float vsF[256][4]; int vsI[16][4]; BOOL vsB[16];
    float psF[224][4]; int psI[16][4]; BOOL psB[16];
    RECT scissor;
    float npatch;
    UINT palette;
};

namespace {

template <typename T> uint32_t Val(T* o) {
    if (o) o->Release();   // Get* added a reference; the value is all that is kept
    return (uint32_t)(uintptr_t)o;
}

DevState* RecordState(IDirect3DDevice9* d) {
    auto* s = new DevState();
    for (int i = 0; i < NRS; i++) s->rsOk[i] = SUCCEEDED(d->GetRenderState((D3DRENDERSTATETYPE)kRenderStates[i], &s->rs[i]));
    for (int t = 0; t < 8; t++)
        for (int i = 0; i < NTSS; i++) d->GetTextureStageState(t, (D3DTEXTURESTAGESTATETYPE)kStageStates[i], &s->tss[t][i]);
    for (int k = 0; k < NSMP; k++) {
        for (int i = 0; i < 13; i++) d->GetSamplerState(kSamplers[k], (D3DSAMPLERSTATETYPE)(i + 1), &s->ss[k][i]);
        IDirect3DBaseTexture9* t = nullptr;
        d->GetTexture(kSamplers[k], &t);
        s->tex[k] = Val(t);
    }
    for (UINT i = 0; i < 16; i++) {
        IDirect3DVertexBuffer9* vb = nullptr;
        d->GetStreamSource(i, &vb, &s->streams[i].off, &s->streams[i].stride);
        s->streams[i].vb = Val(vb);
        d->GetStreamSourceFreq(i, &s->streams[i].freq);
    }
    IDirect3DIndexBuffer9* ib = nullptr; d->GetIndices(&ib); s->ib = Val(ib);
    IDirect3DVertexDeclaration9* decl = nullptr; d->GetVertexDeclaration(&decl); s->decl = Val(decl);
    IDirect3DVertexShader9* vs = nullptr; d->GetVertexShader(&vs); s->vs = Val(vs);
    IDirect3DPixelShader9* ps = nullptr; d->GetPixelShader(&ps); s->ps = Val(ps);
    d->GetFVF(&s->fvf);
    for (DWORD i = 0; i < 4; i++) {
        IDirect3DSurface9* rt = nullptr;
        d->GetRenderTarget(i, &rt);
        s->rt[i] = Val(rt);
    }
    IDirect3DSurface9* ds = nullptr; d->GetDepthStencilSurface(&ds); s->ds = Val(ds);
    for (int i = 0; i < NXF; i++) d->GetTransform(kTransforms[i], &s->xf[i]);
    d->GetViewport(&s->vp);
    d->GetMaterial(&s->mat);
    for (DWORD i = 0; i < 16; i++) {
        s->lightOk[i] = SUCCEEDED(d->GetLight(i, &s->light[i]));
        if (s->lightOk[i]) d->GetLightEnable(i, &s->lightOn[i]);
    }
    for (DWORD i = 0; i < 6; i++) d->GetClipPlane(i, s->clip[i]);
    D3DCAPS9 caps = {};
    d->GetDeviceCaps(&caps);
    s->nvsF = caps.MaxVertexShaderConst < 256 ? caps.MaxVertexShaderConst : 256;
    s->npsF = caps.PixelShaderVersion >= D3DPS_VERSION(3, 0) ? 224 : 32;
    d->GetVertexShaderConstantF(0, &s->vsF[0][0], s->nvsF);
    d->GetVertexShaderConstantI(0, &s->vsI[0][0], 16);
    d->GetVertexShaderConstantB(0, s->vsB, 16);
    d->GetPixelShaderConstantF(0, &s->psF[0][0], s->npsF);
    d->GetPixelShaderConstantI(0, &s->psI[0][0], 16);
    d->GetPixelShaderConstantB(0, s->psB, 16);
    d->GetScissorRect(&s->scissor);
    s->npatch = d->GetNPatchMode();
    d->GetCurrentTexturePalette(&s->palette);
    return s;
}

// ------------------------------------------------------------------ object contents
} // namespace
bool InstallHooks() {
    InitializeCriticalSection(&g_cs);
    g_objs.reserve(16384);
    HMODULE exe = GetModuleHandleA(nullptr);
    g_origDirect3DCreate9 = (decltype(g_origDirect3DCreate9))PatchIat(exe, "d3d9.dll", "Direct3DCreate9",
                                                                       (void*)&H_Direct3DCreate9);
    bool ok = g_origDirect3DCreate9 != nullptr;
    if (!ok) ok = PatchDelayIat(exe, "d3d9.dll", "Direct3DCreate9", (void*)&H_Direct3DCreate9) != nullptr;
    Log("d3d hook: Direct3DCreate9 %s", ok ? (g_origDirect3DCreate9 ? "import patched" : "delay import patched") : "NOT FOUND");
    return ok;
}

IDirect3DDevice9* Device() { return g_dev; }

bool DeviceUsable() { return g_dev && g_dev->TestCooperativeLevel() == D3D_OK; }

uint32_t Generation() { return g_generation; }

void Track(IUnknown* obj, Kind kind, bool defaultPool) { TrackObj(obj, kind, defaultPool, false); }

const char* KindName(int k) {
    static const char* names[K_Count] = {"texture", "volume texture", "cube texture", "vertex buffer", "index buffer",
                                         "render target", "depth stencil", "offscreen surface", "swap chain",
                                         "state block", "vertex decl", "vertex shader", "pixel shader", "query"};
    return k >= 0 && k < K_Count ? names[k] : "?";
}

std::vector<std::pair<IUnknown*, Kind>> TrackedObjects() {
    EnterCriticalSection(&g_cs);
    std::vector<std::pair<IUnknown*, Kind>> v;
    v.reserve(g_objs.size());
    for (auto& [obj, o] : g_objs) v.emplace_back(obj, o.kind);
    LeaveCriticalSection(&g_cs);
    return v;
}

void Collect() {
    EnterCriticalSection(&g_cs);
    for (auto it = g_objs.begin(); it != g_objs.end();) {
        if (!it->second.permanent && it->second.snapRefs == 0 && RefCount(it->first) <= 1) {
            if (!g_surfLocks.empty() || !g_texLocks.empty()) ForgetLocks(it->first);
            g_ourRelease++;
            it->first->Release();
            g_ourRelease--;
            it = g_objs.erase(it);
        } else {
            ++it;
        }
    }
    LeaveCriticalSection(&g_cs);
}

size_t TrackedCount() { return g_objs.size(); }

// A savestate pins what its memory can use: objects the game holds references to, objects bound to the device
// (re-bound by the state block on load), and the "permanent" ones (vertex declarations in RenderWare's cache, which
// holds no references).
Capture* Save() {
    Collect();
    auto* c = new Capture;
    c->generation = g_generation;
    c->dev = RecordState(g_dev);
    std::unordered_set<IUnknown*> bound;
    for (uint32_t v : c->dev->tex) bound.insert((IUnknown*)(uintptr_t)v);
    for (auto& s : c->dev->streams) bound.insert((IUnknown*)(uintptr_t)s.vb);
    for (uint32_t v : {c->dev->ib, c->dev->decl, c->dev->vs, c->dev->ps, c->dev->ds}) bound.insert((IUnknown*)(uintptr_t)v);
    for (uint32_t v : c->dev->rt) bound.insert((IUnknown*)(uintptr_t)v);
    EnterCriticalSection(&g_cs);
    c->refs.reserve(g_objs.size());
    for (auto& [obj, o] : g_objs) {
        uint32_t refs = RefCount(obj) - 1 - o.stateRefs;
        if (!refs && !o.permanent && !bound.count(obj)) continue;
        c->refs.emplace_back(obj, refs);
        if (o.permanent) c->permanent.push_back(obj);
        o.snapRefs++;
    }
    LeaveCriticalSection(&g_cs);
    std::unordered_map<IUnknown*, ULONG> before;
    for (auto& [obj, o] : g_objs) { (void)o; before[obj] = RefCount(obj); }
    if (FAILED(Orig<decltype(&H_CreateStateBlock)>(DEV_CreateStateBlock)(g_dev, D3DSBT_ALL, &c->state))) c->state = nullptr;
    for (auto& [obj, o] : g_objs) {
        const auto after = RefCount(obj);
        if (after > before[obj]) {
            const auto held = after - before[obj];
            o.stateRefs += held;
            c->state_refs.emplace_back(obj, held);
        }
    }

    if (!c->state || !CaptureContents(c)) { Release(c); return nullptr; }
    return c;
}

bool Load(const Capture* c) {
    // Remove current bindings before reducing their application references.
    if (!c->state || FAILED(c->state->Apply())) return false;
    std::unordered_map<IUnknown*, uint32_t> want(c->refs.begin(), c->refs.end());
    std::unordered_set<IUnknown*> perm(c->permanent.begin(), c->permanent.end());
    EnterCriticalSection(&g_cs);
    uint32_t adjusted = 0;
    for (auto& [obj, o] : g_objs) {
        // The restored memory can only reach declarations it could reach at save time.
        if (o.kind == K_VertexDecl || o.kind == K_VertexShader || o.kind == K_PixelShader) o.permanent = perm.count(obj) != 0;
        auto it = want.find(obj);
        uint32_t target = (it == want.end() ? 0 : it->second) + o.stateRefs;
        ULONG cur = obj->AddRef() - 2;   // minus our probe and our pin
        if (cur != target) adjusted++;
        for (; cur < target; cur++) obj->AddRef();
        g_ourRelease++;
        for (; cur > target; cur--) obj->Release();
        g_ourRelease--;
        obj->Release();
    }
    LeaveCriticalSection(&g_cs);
    if (!RestoreContents(c)) return false;
    Log("d3d load: %u tracked, %u in savestate, %u refcounts adjusted", (unsigned)g_objs.size(),
        (unsigned)c->refs.size(), adjusted);
    return true;
}

void Release(Capture* c) {
    if (!c) return;
    EnterCriticalSection(&g_cs);
    for (auto& [obj, held] : c->state_refs) {
        auto it = g_objs.find(obj);
        if (it != g_objs.end()) it->second.stateRefs -= held;
    }
    for (auto& [obj, n] : c->refs) {
        auto it = g_objs.find(obj);
        if (it != g_objs.end() && it->second.snapRefs) it->second.snapRefs--;
    }
    LeaveCriticalSection(&g_cs);
    if (c->state) c->state->Release();
    delete c->dev;
    delete c;
}

IDirect3D9* Direct3D() { return g_d3d; }
} // namespace d3d
