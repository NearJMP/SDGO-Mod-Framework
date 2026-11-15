#pragma once
#include "sdgo/surface_decal_ui.h"
#include <d3d9.h>

namespace sdgo::surface_decal::ui {
// Page-local drawing in the client's existing1024x768 UI coordinate space.
// Native labels/fonts remain native children; this owns no window or texture.
class Canvas {
    struct Vertex{float x,y,z,rhw;DWORD color;float u,v;};
    IDirect3DDevice9* device_=nullptr;IDirect3DStateBlock9* saved_=nullptr;IDirect3DVertexBuffer9* stream_=nullptr;
    UINT offset_=0,stride_=0;float sx_=1,sy_=1;bool streamCaptured_=false;
public:
    bool ready=false;
    explicit Canvas(IDirect3DDevice9* d):device_(d){
        if(!d)return;IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
        if(FAILED(d->GetRenderTarget(0,&target))||!target)return;
        const auto hr=target->GetDesc(&desc);target->Release();
        if(FAILED(hr)||!desc.Width||!desc.Height||FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved_))||!saved_)return;
        streamCaptured_=SUCCEEDED(d->GetStreamSource(0,&stream_,&offset_,&stride_));sx_=desc.Width/1024.f;sy_=desc.Height/768.f;
        const D3DVIEWPORT9 viewport{0,0,desc.Width,desc.Height,0,1};d->SetViewport(&viewport);
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
        d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        for(auto a:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(0,a,D3DTADDRESS_CLAMP);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);ready=true;
    }
    ~Canvas(){if(saved_){saved_->Apply();saved_->Release();}if(streamCaptured_)device_->SetStreamSource(0,stream_,offset_,stride_);if(stream_)stream_->Release();}
    void Quad(Rect r,DWORD color,IDirect3DBaseTexture9* texture=nullptr){
        if(!ready||r.w<=0||r.h<=0)return;const float x=r.x*sx_-.5f,y=r.y*sy_-.5f,w=r.w*sx_,h=r.h*sy_;
        const Vertex v[]={{x,y,0,1,color,0,0},{x+w,y,0,1,color,1,0},{x,y+h,0,1,color,0,1},{x+w,y+h,0,1,color,1,1}};
        device_->SetTexture(0,texture);device_->SetTextureStageState(0,D3DTSS_COLOROP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);device_->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_TEXTURE);
        device_->SetTextureStageState(0,D3DTSS_ALPHAOP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);device_->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_TEXTURE);
        device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));
    }
    void Border(Rect r,DWORD c){Quad({r.x,r.y,r.w,1},c);Quad({r.x,r.y+r.h-1,r.w,1},c);Quad({r.x,r.y,1,r.h},c);Quad({r.x+r.w-1,r.y,1,r.h},c);}
};
}
