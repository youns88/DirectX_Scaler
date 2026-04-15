#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

// Shader with Cropping Logic: It only samples the game window area from the desktop texture
const char* shaderSource = R"(
    struct VS_IN { float4 pos : POSITION; float2 uv : TEXCOORD; };
    struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
    cbuffer CropData : register(b0) { float4 crop; }; // x, y, width, height in UV units
    
    PS_IN vs_main(VS_IN input) {
        PS_IN output;
        output.pos = input.pos;
        // Transform standard 0-1 UVs to the specific crop area of the desktop
        output.uv = float2(crop.x + input.uv.x * crop.z, crop.y + input.uv.y * crop.w);
        return output;
    }
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    float4 ps_main(PS_IN input) : SV_Target {
        return tex.Sample(samp, input.uv);
    }
)";

struct Vertex { float x, y, z, w, u, v; };
struct CropCB { float x, y, w, h; };

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) return 0;

    RECT gr; GetClientRect(gameHwnd, &gr);
    POINT tl = {0, 0}; ClientToScreen(gameHwnd, &tl);
    int gameW = gr.right - gr.left, gameH = gr.bottom - gr.top;
    int upW = (int)(gameW * SCALE), upH = (int)(gameH * SCALE);
    int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"SharpScaler", WS_POPUP,
        (scrW - upW) / 2, (scrH - upH) / 2, upW, upH, NULL, NULL, hInstance, NULL);

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);

    // D3D11 Setup
    ID3D11Device* dev; ID3D11DeviceContext* ctx; IDXGISwapChain* swap;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 2; sd.BufferDesc.Width = upW; sd.BufferDesc.Height = upH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &swap, &dev, NULL, &ctx);

    ID3D11RenderTargetView* rtv; ID3D11Texture2D* bb;
    swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    dev->CreateRenderTargetView(bb, NULL, &rtv); bb->Release();

    // Shaders & Constant Buffer for Cropping
    ID3DBlob *vsB, *psB;
    D3DCompile(shaderSource, strlen(shaderSource), 0, 0, 0, "vs_main", "vs_4_0", 0, 0, &vsB, 0);
    D3DCompile(shaderSource, strlen(shaderSource), 0, 0, 0, "ps_main", "ps_4_0", 0, 0, &psB, 0);
    ID3D11VertexShader* vs; ID3D11PixelShader* ps;
    dev->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), 0, &vs);
    dev->CreatePixelShader(psB->GetBufferPointer(), psB->GetBufferSize(), 0, &ps);

    CropCB cbData = { (float)tl.x / scrW, (float)tl.y / scrH, (float)gameW / scrW, (float)gameH / scrH };
    ID3D11Buffer* cb;
    D3D11_BUFFER_DESC cbd = { sizeof(CropCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
    dev->CreateBuffer(&cbd, 0, &cb);

    ID3D11InputLayout* lay;
    D3D11_INPUT_ELEMENT_DESC ied[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}, {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0} };
    dev->CreateInputLayout(ied, 2, vsB->GetBufferPointer(), vsB->GetBufferSize(), &lay);

    Vertex v[] = { {-1,1,0,1,0,0}, {1,1,0,1,1,0}, {-1,-1,0,1,0,1}, {1,-1,0,1,1,1} };
    ID3D11Buffer* vb; D3D11_BUFFER_DESC vbd = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA vsrd = { v }; dev->CreateBuffer(&vbd, &vsrd, &vb);

    // Duplication API
    IDXGIDevice* dxgiD; dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiD);
    IDXGIAdapter* adp; dxgiD->GetParent(__uuidof(IDXGIAdapter), (void**)&adp);
    IDXGIOutput* out; adp->EnumOutputs(0, &out);
    IDXGIOutput1* out1; out->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    IDXGIOutputDuplication* dupl; out1->DuplicateOutput(dev, &dupl);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        
        IDXGIResource* res; DXGI_OUTDUPL_FRAME_INFO info;
        if (SUCCEEDED(dupl->AcquireNextFrame(16, &info, &res))) {
            ID3D11Texture2D* tex; res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
            ID3D11ShaderResourceView* srv; dev->CreateShaderResourceView(tex, NULL, &srv);

            // Update crop window position in case game moved
            GetWindowRect(gameHwnd, &gr); tl = {0,0}; ClientToScreen(gameHwnd, &tl);
            cbData = { (float)tl.x / scrW, (float)tl.y / scrH, (float)gameW / scrW, (float)gameH / scrH };
            D3D11_MAPPED_SUBRESOURCE map;
            ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map); memcpy(map.pData, &cbData, sizeof(cbData)); ctx->Unmap(cb, 0);

            ctx->OMSetRenderTargets(1, &rtv, NULL);
            D3D11_VIEWPORT vp = {0, 0, (float)upW, (float)upH, 0, 1}; ctx->RSSetViewports(1, &vp);
            ctx->IASetInputLayout(lay);
            UINT stride = sizeof(Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx->VSSetShader(vs, 0, 0); ctx->VSSetConstantBuffers(0, 1, &cb);
            ctx->PSSetShader(ps, 0, 0); ctx->PSSetShaderResources(0, 1, &srv);
            ctx->Draw(4, 0);

            swap->Present(1, 0);
            srv->Release(); tex->Release(); res->Release(); dupl->ReleaseFrame();
        }
    }

    // CLEANUP: Release everything to clear RAM
    dupl->Release(); out1->Release(); out->Release(); adp->Release(); dxgiD->Release();
    vb->Release(); cb->Release(); lay->Release(); vs->Release(); ps->Release();
    rtv->Release(); swap->Release(); ctx->Release(); dev->Release();
    
    return (int)msg.wParam;
}
