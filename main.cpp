#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

IDXGIOutputDuplication* dup = nullptr;
ID3D11Device* dev = nullptr;
ID3D11DeviceContext* ctx = nullptr;
IDXGISwapChain* sc = nullptr;
ID3D11RenderTargetView* rtv = nullptr;
ID3D11SamplerState* samp = nullptr;
ID3D11ShaderResourceView* srv = nullptr;
ID3D11Texture2D* cropTex = nullptr;
ID3D11VertexShader* vs = nullptr;
ID3D11PixelShader* ps = nullptr;
ID3D11InputLayout* layout = nullptr;
ID3D11Buffer* vb = nullptr;

HWND wnd;
bool run = true;

const char* TARGET = "DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";

struct V { float x,y,u,v; };

RECT GetRect(HWND w) {
    RECT r={0};
    GetWindowRect(w,&r);
    return r;
}

HWND GetTarget() {
    return FindWindowA(0, TARGET);
}

void InitD3D(HWND h,int W,int H){
    DXGI_SWAP_CHAIN_DESC sd={};
    sd.BufferCount=2;
    sd.BufferDesc.Width=W;
    sd.BufferDesc.Height=H;
    sd.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow=h;
    sd.SampleDesc.Count=1;
    sd.Windowed=1;
    sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,0,0,
        D3D11_SDK_VERSION,&sd,&sc,&dev,0,&ctx);

    ID3D11Texture2D* bb;
    sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);
    dev->CreateRenderTargetView(bb,0,&rtv);
    bb->Release();

    IDXGIDevice* dx;
    dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&dx);
    IDXGIAdapter* ad;
    dx->GetAdapter(&ad);
    IDXGIOutput* out;
    ad->EnumOutputs(0,&out);
    IDXGIOutput1* out1;
    out->QueryInterface(__uuidof(IDXGIOutput1),(void**)&out1);
    out1->DuplicateOutput(dev,&dup);

    dx->Release(); ad->Release(); out->Release(); out1->Release();
}

void Shaders(){
    const char* v=
    "struct V{float2 p:POSITION;float2 t:TEXCOORD;};"
    "struct O{float4 p:SV_POSITION;float2 t:TEXCOORD;};"
    "O main(V i){O o;o.p=float4(i.p,0,1);o.t=i.t;return o;}";

    const char* p=
    "Texture2D tx:register(t0);SamplerState s:register(s0);"
    "float4 main(float4 p:SV_POSITION,float2 t:TEXCOORD):SV_Target{return tx.Sample(s,t);}";

    ID3DBlob *vb1,*pb1;
    D3DCompile(v,strlen(v),0,0,0,"main","vs_4_0",0,0,&vb1,0);
    D3DCompile(p,strlen(p),0,0,0,"main","ps_4_0",0,0,&pb1,0);

    dev->CreateVertexShader(vb1->GetBufferPointer(),vb1->GetBufferSize(),0,&vs);
    dev->CreatePixelShader(pb1->GetBufferPointer(),pb1->GetBufferSize(),0,&ps);

    D3D11_INPUT_ELEMENT_DESC ld[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}
    };

    dev->CreateInputLayout(ld,2,vb1->GetBufferPointer(),vb1->GetBufferSize(),&layout);

    vb1->Release(); pb1->Release();

    D3D11_SAMPLER_DESC s={};
    s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&s,&samp);
}

ID3D11Texture2D* Capture(){
    IDXGIResource* r;
    DXGI_OUTDUPL_FRAME_INFO i;

    if(dup->AcquireNextFrame(16,&i,&r)!=S_OK) return 0;

    ID3D11Texture2D* t;
    r->QueryInterface(__uuidof(ID3D11Texture2D),(void**)&t);

    dup->ReleaseFrame();
    r->Release();
    return t;
}

void Crop(ID3D11Texture2D* src, RECT wr, HWND target){
    POINT p={wr.left,wr.top};
    MapWindowPoints(HWND_DESKTOP,0,&p,1);

    int w=wr.right-wr.left;
    int h=wr.bottom-wr.top;

    if(!cropTex){
        D3D11_TEXTURE2D_DESC d;
        src->GetDesc(&d);
        d.Width=w; d.Height=h;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        dev->CreateTexture2D(&d,0,&cropTex);
    }

    D3D11_BOX b={};
    b.left=p.x; b.top=p.y;
    b.right=p.x+w; b.bottom=p.y+h;
    b.front=0; b.back=1;

    ctx->CopySubresourceRegion(cropTex,0,0,0,0,src,0,&b);
}

void Draw(){
    V vtx[6]={
        {-1,1,0,0},{1,1,1,0},{1,-1,1,1},
        {-1,1,0,0},{1,-1,1,1},{-1,-1,0,1}
    };

    if(vb) vb->Release();

    D3D11_BUFFER_DESC bd={};
    bd.Usage=D3D11_USAGE_DYNAMIC;
    bd.ByteWidth=sizeof(vtx);
    bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;

    dev->CreateBuffer(&bd,0,&vb);

    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(vb,0,D3D11_MAP_WRITE_DISCARD,0,&m);
    memcpy(m.pData,vtx,sizeof(vtx));
    ctx->Unmap(vb,0);

    UINT s=sizeof(V),o=0;
    ctx->IASetVertexBuffers(0,1,&vb,&s,&o);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(layout);

    ctx->VSSetShader(vs,0,0);
    ctx->PSSetShader(ps,0,0);
    ctx->PSSetSamplers(0,1,&samp);
    ctx->PSSetShaderResources(0,1,&srv);

    ctx->OMSetRenderTargets(1,&rtv,0);

    float c[4]={0,0,0,0};
    ctx->ClearRenderTargetView(rtv,c);

    ctx->Draw(6,0);
    sc->Present(1,0);
}

void Clean(){
    if(dup) dup->Release();
    if(cropTex) cropTex->Release();
    if(srv) srv->Release();
    if(vb) vb->Release();
    if(samp) samp->Release();
    if(rtv) rtv->Release();
    if(sc) sc->Release();
    if(ctx) ctx->Release();
    if(dev) dev->Release();
}

LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_DESTROY){run=0;PostQuitMessage(0);}
    return DefWindowProc(h,m,w,l);
}

int WINAPI WinMain(HINSTANCE h,HINSTANCE,LPSTR,int){
    HWND tgt=0;
    while(!(tgt=GetTarget())) Sleep(300);

    RECT r=GetRect(tgt);
    int w=(r.right-r.left)*1.6f;
    int hgt=(r.bottom-r.top)*1.6f;

    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);

    int x=(sw-w)/2;
    int y=(sh-hgt)/2;

    WNDCLASS wc={};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=h;
    wc.lpszClassName="ov";
    RegisterClass(&wc);

    wnd=CreateWindowEx(
        WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT,
        "ov","",WS_POPUP,x,y,w,hgt,0,0,h,0);

    SetLayeredWindowAttributes(wnd,0,255,LWA_ALPHA);
    ShowWindow(wnd,1);

    InitD3D(wnd,w,hgt);
    Shaders();

    MSG msg={};

    while(run){
        while(PeekMessage(&msg,0,0,0,PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ID3D11Texture2D* f=Capture();
        if(f){
            RECT rr=GetRect(tgt);
            Crop(f,rr,tgt);

            if(srv) srv->Release();
            dev->CreateShaderResourceView(cropTex,0,&srv);

            Draw();
            f->Release();
        }
    }

    Clean();
    return 0;
}
