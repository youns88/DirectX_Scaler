#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

IDXGIOutputDuplication* duplication = nullptr;
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
IDXGISwapChain* swapChain = nullptr;
ID3D11RenderTargetView* rtv = nullptr;
ID3D11SamplerState* sampler = nullptr;
ID3D11ShaderResourceView* srv = nullptr;
ID3D11VertexShader* vs = nullptr;
ID3D11PixelShader* ps = nullptr;
ID3D11InputLayout* layout = nullptr;
ID3D11Buffer* vb = nullptr;

ID3D11Texture2D* croppedTex = nullptr;

HWND overlay;
bool running = true;

const char* TARGET = "DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";

struct VERTEX {
    float x, y;
    float u, v;
};

RECT GetGameRect() {
    HWND hwnd = FindWindowA(nullptr, TARGET);
    RECT r = {0};
    if (hwnd) GetWindowRect(hwnd, &r);
    return r;
}

void InitD3D(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &sd,
        &swapChain,
        &device,
        nullptr,
        &context
    );

    ID3D11Texture2D* backBuffer;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    backBuffer->Release();

    IDXGIDevice* dxgiDevice;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    IDXGIAdapter* adapter;
    dxgiDevice->GetAdapter(&adapter);

    IDXGIOutput* output;
    adapter->EnumOutputs(0, &output);

    IDXGIOutput1* output1;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);

    output1->DuplicateOutput(device, &duplication);

    dxgiDevice->Release();
    adapter->Release();
    output->Release();
    output1->Release();
}

void CreateShaders() {
    const char* vsCode =
        "struct VS_IN { float2 pos : POSITION; float2 uv : TEXCOORD; };"
        "struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };"
        "PS_IN main(VS_IN i){ PS_IN o; o.pos=float4(i.pos,0,1); o.uv=i.uv; return o;}";

    const char* psCode =
        "Texture2D tex : register(t0);"
        "SamplerState samp : register(s0);"
        "float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD):SV_Target{ return tex.Sample(samp,uv);}";

    ID3DBlob* vsBlob;
    ID3DBlob* psBlob;

    D3DCompile(vsCode, strlen(vsCode), 0, 0, 0, "main", "vs_4_0", 0, 0, &vsBlob, 0);
    D3DCompile(psCode, strlen(psCode), 0, 0, 0, "main", "ps_4_0", 0, 0, &psBlob, 0);

    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}
    };

    device->CreateInputLayout(layoutDesc, 2, vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), &layout);

    vsBlob->Release();
    psBlob->Release();

    D3D11_SAMPLER_DESC s = {};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    device->CreateSamplerState(&s, &sampler);
}

ID3D11Texture2D* Capture() {
    IDXGIResource* res;
    DXGI_OUTDUPL_FRAME_INFO info;

    if (duplication->AcquireNextFrame(0, &info, &res) != S_OK)
        return nullptr;

    ID3D11Texture2D* tex;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);

    duplication->ReleaseFrame();
    res->Release();

    return tex;
}

// ZERO-COPY GPU CROP
void CropTexture(ID3D11Texture2D* src, RECT r) {
    if (croppedTex) { croppedTex->Release(); croppedTex = nullptr; }

    int w = r.right - r.left;
    int h = r.bottom - r.top;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    device->CreateTexture2D(&desc, nullptr, &croppedTex);

    D3D11_BOX box = {};
    box.left = r.left;
    box.top = r.top;
    box.right = r.right;
    box.bottom = r.bottom;
    box.front = 0;
    box.back = 1;

    context->CopySubresourceRegion(croppedTex, 0, 0, 0, 0, src, 0, &box);
}

void Render(ID3D11Texture2D* frame) {
    RECT r = GetGameRect();
    if (r.right == 0) return;

    CropTexture(frame, r);

    if (srv) srv->Release();
    device->CreateShaderResourceView(croppedTex, nullptr, &srv);

    VERTEX verts[6] = {
        {-1,1, 0,0},
        {1,1, 1,0},
        {1,-1,1,1},
        {-1,1,0,0},
        {1,-1,1,1},
        {-1,-1,0,1}
    };

    if (vb) vb->Release();

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(verts);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateBuffer(&bd, nullptr, &vb);

    D3D11_MAPPED_SUBRESOURCE ms;
    context->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    memcpy(ms.pData, verts, sizeof(verts));
    context->Unmap(vb, 0);

    UINT stride = sizeof(VERTEX), offset = 0;

    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(layout);

    context->VSSetShader(vs, 0, 0);
    context->PSSetShader(ps, 0, 0);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetShaderResources(0, 1, &srv);

    context->OMSetRenderTargets(1, &rtv, nullptr);

    float clear[4] = {0,0,0,0};
    context->ClearRenderTargetView(rtv, clear);

    context->Draw(6, 0);

    swapChain->Present(1, 0); // frame pacing via vsync
}

void Cleanup() {
    if (duplication) duplication->Release();
    if (croppedTex) croppedTex->Release();
    if (srv) srv->Release();
    if (vb) vb->Release();
    if (sampler) sampler->Release();
    if (rtv) rtv->Release();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        running = false;
        PostQuitMessage(0);
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    RECT r;
    while (true) {
        r = GetGameRect();
        if (r.right != 0) break;
        Sleep(500);
    }

    int w = (int)((r.right - r.left) * 1.6f);
    int h = (int)((r.bottom - r.top) * 1.6f);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "overlay";
    RegisterClass(&wc);

    overlay = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        "overlay","",
        WS_POPUP,
        x,y,w,h,
        0,0,hInst,0
    );

    SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
    ShowWindow(overlay, SW_SHOW);

    InitD3D(overlay, w, h);
    CreateShaders();

    MSG msg = {};

    while (running) {
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ID3D11Texture2D* frame = Capture();
        if (frame) {
            Render(frame);
            frame->Release();
        }
    }

    Cleanup();
    return 0;
}
