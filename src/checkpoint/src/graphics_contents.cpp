#include "graphics.hpp"
#include <cstring>
namespace d3d {
bool CaptureContents(Capture* c) {
 for(auto [object,refs]:c->refs) {
  (void)refs;
  D3DRESOURCETYPE type{};
  // QueryInterface avoids interpreting shaders/state blocks as resources.
  IDirect3DResource9* resource{};
  if(FAILED(object->QueryInterface(IID_IDirect3DResource9,(void**)&resource))) continue;
  type=resource->GetType(); resource->Release();
  if(type==D3DRTYPE_TEXTURE) {
   auto t=(IDirect3DTexture9*)object;
   for(UINT level=0;level<t->GetLevelCount();++level) {
    D3DSURFACE_DESC desc{}; if(FAILED(t->GetLevelDesc(level,&desc))) return false;
    if(desc.Pool!=D3DPOOL_MANAGED&&desc.Pool!=D3DPOOL_SYSTEMMEM) return false;
    if(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8) return false;
    D3DLOCKED_RECT locked{};
    if(FAILED(t->LockRect(level,&locked,nullptr,D3DLOCK_READONLY))) return false;
    Content copy{object,K_Texture,level,desc.Width*4,desc.Height,{}};
    copy.bytes.resize((size_t)copy.row_bytes*copy.rows);
    for(UINT row=0;row<copy.rows;++row)
     memcpy(copy.bytes.data()+(size_t)row*copy.row_bytes,(uint8_t*)locked.pBits+(size_t)row*locked.Pitch,copy.row_bytes);
    t->UnlockRect(level); c->contents.push_back(std::move(copy));
   }
  } else if(type==D3DRTYPE_VERTEXBUFFER||type==D3DRTYPE_INDEXBUFFER) {
   UINT size{}; void* p{}; HRESULT hr{};
   if(type==D3DRTYPE_VERTEXBUFFER) {
    D3DVERTEXBUFFER_DESC d{}; ((IDirect3DVertexBuffer9*)object)->GetDesc(&d); size=d.Size;
    hr=((IDirect3DVertexBuffer9*)object)->Lock(0,0,&p,D3DLOCK_READONLY);
   } else {
    D3DINDEXBUFFER_DESC d{}; ((IDirect3DIndexBuffer9*)object)->GetDesc(&d); size=d.Size;
    hr=((IDirect3DIndexBuffer9*)object)->Lock(0,0,&p,D3DLOCK_READONLY);
   }
   if(FAILED(hr)) return false;
   Content copy{object,type==D3DRTYPE_VERTEXBUFFER?K_VertexBuffer:K_IndexBuffer,0,size,1,{}};
   copy.bytes.assign((uint8_t*)p,(uint8_t*)p+size);
   if(type==D3DRTYPE_VERTEXBUFFER) ((IDirect3DVertexBuffer9*)object)->Unlock();
   else ((IDirect3DIndexBuffer9*)object)->Unlock();
   c->contents.push_back(std::move(copy));
  }
 }
 return true;
}
bool RestoreContents(const Capture* c) {
 for(auto& copy:c->contents) {
  if(copy.kind==K_Texture) {
   auto t=(IDirect3DTexture9*)copy.object; D3DLOCKED_RECT lr{};
   if(FAILED(t->LockRect(copy.level,&lr,nullptr,0))) return false;
   for(UINT row=0;row<copy.rows;++row)
    memcpy((uint8_t*)lr.pBits+(size_t)row*lr.Pitch,copy.bytes.data()+(size_t)row*copy.row_bytes,copy.row_bytes);
   t->UnlockRect(copy.level);
  } else {
   void* p{}; HRESULT hr=copy.kind==K_VertexBuffer?
    ((IDirect3DVertexBuffer9*)copy.object)->Lock(0,0,&p,0):
    ((IDirect3DIndexBuffer9*)copy.object)->Lock(0,0,&p,0);
   if(FAILED(hr)) return false;
   memcpy(p,copy.bytes.data(),copy.bytes.size());
   if(copy.kind==K_VertexBuffer) ((IDirect3DVertexBuffer9*)copy.object)->Unlock();
   else ((IDirect3DIndexBuffer9*)copy.object)->Unlock();
  }
 }
 return true;
}
}

