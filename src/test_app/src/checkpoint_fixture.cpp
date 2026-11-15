// Ordinary Win32/D3D9/DirectSound application. No checkpoint API or runtime link.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <dsound.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr DWORD pcm_bytes = 48000 * 2 * 8;
constexpr DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
struct Vertex { float x,y,z,rhw; DWORD color; float u,v; };
struct HeapState { int x,y,score; uint32_t rng; };
struct Scene {
    IDirect3DTexture9* texture{};
    IDirect3DVertexBuffer9* vertices{};
    IDirect3DIndexBuffer9* indices{};
    IDirectSoundBuffer* sound{};
};
HWND window{};
IDirect3D9* d3d{};
IDirect3DDevice9* device{};
IDirectSound8* sound_device{};
IDirect3DTexture9* shared_texture{};
IDirectSoundBuffer* shared_music{};
Scene scene;
HeapState* world{};
int level{};
uint64_t static_frames{};
bool running=true,change_level{};
template<class T> void release(T*& p) { if(p) p->Release(); p=nullptr; }
void require(HRESULT result) {
    if (FAILED(result)) {
        wchar_t text[80]; swprintf_s(text,L"Fixture API failed: 0x%08lX",static_cast<unsigned long>(result));
        MessageBoxW(window,text,L"Checkpoint fixture",MB_ICONERROR);
        ExitProcess(2);
    }
}
void fill_texture(IDirect3DTexture9* texture, bool shared) {
    D3DLOCKED_RECT locked{}; require(texture->LockRect(0,&locked,nullptr,0));
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const bool stripe=((x/8+y/8)%2)!=0;
        DWORD color=level==0?(stripe?0xff4fd1e8u:0xff164e86u):(stripe?0xffffc143u:0xffb03825u);
        if(!shared) color=level==0?0xff8aefb0u:0xffed86d3u;
        reinterpret_cast<DWORD*>(static_cast<uint8_t*>(locked.pBits)+y*locked.Pitch)[x]=color;
    }
    require(texture->UnlockRect(0));
}
IDirectSoundBuffer* make_sound() {
    WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=1;
    format.nSamplesPerSec=48000; format.wBitsPerSample=16;
    format.nBlockAlign=2; format.nAvgBytesPerSec=96000;
    DSBUFFERDESC description{}; description.dwSize=sizeof(description);
    description.dwFlags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_CTRLFREQUENCY|DSBCAPS_GLOBALFOCUS;
    description.dwBufferBytes=pcm_bytes; description.lpwfxFormat=&format;
    IDirectSoundBuffer* buffer{}; require(sound_device->CreateSoundBuffer(&description,&buffer,nullptr));
    return buffer;
}
void fill_sound(IDirectSoundBuffer* buffer,bool music) {
    void* p{};void* q{};DWORD n{},m{};
    require(buffer->Lock(0,pcm_bytes,&p,&n,&q,&m,DSBLOCK_ENTIREBUFFER));
    auto write=[music](void* destination,DWORD bytes,DWORD offset) {
        auto samples=static_cast<int16_t*>(destination);
        for(DWORD i=0;i<bytes/2;++i) {
            const double t=static_cast<double>(offset/2+i)/48000.0;
            const int note=static_cast<int>(t*2)%4;
            const double base=level==0?220.0:493.883;
            const double hz=music?base*std::pow(2.0,note/12.0):base*2;
            samples[i]=static_cast<int16_t>(std::sin(6.283185307179586*hz*t)*(music?3500:1200));
        }
    };
    write(p,n,0);if(q)write(q,m,n);
    require(buffer->Unlock(p,n,q,m));
}
void destroy_scene() {
    if(scene.sound) scene.sound->Stop();
    release(scene.sound);release(scene.indices);release(scene.vertices);release(scene.texture);
}
void create_scene() {
    require(device->CreateTexture(64,64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&scene.texture,nullptr));
    fill_texture(scene.texture,false);fill_texture(shared_texture,true);
    require(device->CreateVertexBuffer(sizeof(Vertex)*4,0,fvf,D3DPOOL_MANAGED,&scene.vertices,nullptr));
    require(device->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&scene.indices,nullptr));
    void* indices{};require(scene.indices->Lock(0,12,&indices,0));
    constexpr uint16_t triangles[]={0,1,2,0,2,3};memcpy(indices,triangles,sizeof(triangles));
    require(scene.indices->Unlock());
    scene.sound=make_sound();fill_sound(scene.sound,false);
    require(scene.sound->Play(0,0,DSBPLAY_LOOPING));
    shared_music->Stop();fill_sound(shared_music,true);
    require(shared_music->SetCurrentPosition(0));require(shared_music->Play(0,0,DSBPLAY_LOOPING));
}
void draw_quad(IDirect3DTexture9* texture,float x,float y,float width,float height) {
    const DWORD color=level==0?0xffc8fff0u:0xffffc8d0u;
    Vertex vertices[]={{x,y,0,1,color,0,0},{x+width,y,0,1,color,1,0},
        {x+width,y+height,0,1,color,1,1},{x,y+height,0,1,color,0,1}};
    void* data{};require(scene.vertices->Lock(0,sizeof(vertices),&data,0));
    memcpy(data,vertices,sizeof(vertices));require(scene.vertices->Unlock());
    require(device->SetTexture(0,texture));
    require(device->SetStreamSource(0,scene.vertices,0,sizeof(Vertex)));
    require(device->SetIndices(scene.indices));require(device->SetFVF(fvf));
    require(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_DESTROY) { running=false;PostQuitMessage(0);return 0; }
    if(message==WM_KEYDOWN && (l&(1LL<<30))==0) {
        if(w=='N') change_level=true;
        if(w==VK_SPACE) world->score+=100;
        if(w==VK_ESCAPE) DestroyWindow(hwnd);
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=procedure;
    wc.lpszClassName=L"TimeTransle.CheckpointFixture";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"Checkpoint fixture",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        700,150,900,650,nullptr,nullptr,instance,nullptr);
    // Exercise the ordinary heap APIs imported by this executable (static CRT).
    auto heap=GetProcessHeap();world=static_cast<HeapState*>(HeapAlloc(heap,HEAP_ZERO_MEMORY,sizeof(HeapState)));
    world=static_cast<HeapState*>(HeapReAlloc(heap,HEAP_ZERO_MEMORY,world,sizeof(HeapState)+32));
    if(HeapSize(heap,0,world)<sizeof(HeapState)) return 3;
    world->x=200;world->y=180;world->rng=0x12345678;
    d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d)return 4;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    require(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device));
    require(DirectSoundCreate8(nullptr,&sound_device,nullptr));
    require(sound_device->SetCooperativeLevel(window,DSSCL_PRIORITY));
    require(device->CreateTexture(64,64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&shared_texture,nullptr));
    shared_music=make_sound();create_scene();
    require(device->SetRenderState(D3DRS_LIGHTING,FALSE));require(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    require(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE));
    require(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
    require(device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE));
    ShowWindow(window,show);
    uint64_t stack_frames=0; // Application state deliberately resident on the main thread stack.
    LARGE_INTEGER frequency{},last{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);
    while(running) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        if(!running)break;
        if(change_level) { change_level=false;destroy_scene();level=1-level;
            world->x=400;world->y=320;world->score+=1000;create_scene(); }
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);
        if(now.QuadPart-last.QuadPart>=frequency.QuadPart/60) {
            last=now;++stack_frames;++static_frames;world->rng=world->rng*1664525u+1013904223u;
            if(GetAsyncKeyState(VK_LEFT)&0x8000)world->x-=3;
            if(GetAsyncKeyState(VK_RIGHT)&0x8000)world->x+=3;
            if(GetAsyncKeyState(VK_UP)&0x8000)world->y-=3;
            if(GetAsyncKeyState(VK_DOWN)&0x8000)world->y+=3;
        }
        DWORD position{};shared_music->GetCurrentPosition(&position,nullptr);
        void* pcm{};void* tail{};DWORD bytes{},tail_bytes{};int sample=0;
        require(shared_music->Lock(0,4,&pcm,&bytes,&tail,&tail_bytes,0));
        sample=static_cast<int16_t*>(pcm)[1];require(shared_music->Unlock(pcm,bytes,tail,tail_bytes));
        wchar_t title[256];swprintf_s(title,L"Checkpoint | %c | pos %d,%d | score %d | rng %08X | static %llu stack %llu | PCM %lu sample %d | N:level arrows:move Space:score",
            level==0?L'A':L'B',world->x,world->y,world->score,world->rng,static_frames,stack_frames,position,sample);
        SetWindowTextW(window,title);
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET,level==0?0xff081e35u:0xff351708u,1,0));
        require(device->BeginScene());draw_quad(shared_texture,40,40,780,450);
        draw_quad(scene.texture,static_cast<float>(world->x),static_cast<float>(world->y),80,80);
        require(device->EndScene());require(device->Present(nullptr,nullptr,nullptr,nullptr));
        HDC dc=GetDC(window);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,240,240));
        TextOutW(dc,35,510,title,static_cast<int>(wcslen(title)));ReleaseDC(window,dc);
        Sleep(1);
    }
    destroy_scene();shared_music->Stop();release(shared_music);release(shared_texture);
    release(sound_device);release(device);release(d3d);HeapFree(heap,0,world);
    return 0;
}
