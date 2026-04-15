#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Fix for Windows 10 redirection issues
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

const char* shaderSource = R"(
    struct VS_IN { float4 pos : POSITION; float2 uv : TEXCOORD; };
    struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
    PS_IN vs_main(VS_IN input) {
        PS_IN output; output.pos = input.pos; output.uv = input.uv;
        return output;
    }
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    float4 ps_main(PS_IN input) : SV_Target {
        return tex.Sample(samp, input.uv);
    }
)";

struct Vertex { float x, y, z, w, u, v; };

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) return 0;

    RECT gr; GetClientRect(gameHwnd, &gr);
    POINT tl = {0, 0}; ClientToScreen(gameHwnd, &tl);
    int upW = (int)((gr.right - gr.left) * SCALE);
    int upH = (int)((gr.bottom - gr.top) * SCALE);

    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    // FIXED: Added WS_EX_NOREDIRECTIONBITMAP to fix Win10 dark area
    // FIXED: Added WS_EX_TRANSPARENT to allow mouse clicks to pass through
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"SharpScaler",
        WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - upW) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - upH) / 2,
        upW, upH,
        NULL, NULL, hInstance, NULL
    );

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);

    // D3D11 Setup
    ID3D11Device* dev; ID3D11DeviceContext* ctx; IDXGISwapChain* swap;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = upW; sd.BufferDesc.Height = upH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &swap, &dev, NULL, &ctx);

    ID3D11RenderTargetView* rtv;
    ID3D11Texture2D* backBuf;
    swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuf);
    dev->CreateRenderTargetView(backBuf, NULL, &rtv);
    backBuf->Release();

    // Shaders
    ID3DBlob *vsBlob, *psBlob;
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "vs_main", "vs_4_0", 0, 0, &vsBlob, NULL);
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "ps_main", "ps_4_0", 0, 0, &psBlob, NULL);
    ID3D11VertexShader* vs; ID3D11PixelShader* ps;
    dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &vs);
    dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &ps);

    ID3D11InputLayout* layout;
    D3D11_INPUT_ELEMENT_DESC ied[] = { {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0} };
    dev->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);

    // Quad
    Vertex verts[] = { {-1,1,0,1,0,0}, {1,1,0,1,1,0}, {-1,-1,0,1,0,1}, {1,-1,0,1,1,1} };
    ID3D11Buffer* vb;
    D3D11_BUFFER_DESC bd = { sizeof(verts), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA srd = { verts };
    dev->CreateBuffer(&bd, &srd, &vb);

    // Duplication
    IDXGIDevice* dxgiDev; dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    IDXGIAdapter* adp; dxgiDev->GetParent(__uuidof(IDXGIAdapter), (void**)&adp);
    IDXGIOutput* out; adp->EnumOutputs(0, &out);
    IDXGIOutput1* out1; out->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    IDXGIOutputDuplication* dupl; out1->DuplicateOutput(dev, &dupl);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        
        IDXGIResource* res; DXGI_OUTDUPL_FRAME_INFO info;
        if (SUCCEEDED(dupl->AcquireNextFrame(10, &info, &res))) {
            ID3D11Texture2D* tex; res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
            ID3D11ShaderResourceView* srv;
            dev->CreateShaderResourceView(tex, NULL, &srv);

            ctx->OMSetRenderTargets(1, &rtv, NULL);
            float clear[] = {0, 0, 0, 1}; ctx->ClearRenderTargetView(rtv, clear);
            D3D11_VIEWPORT vp = {0, 0, (float)upW, (float)upH, 0, 1}; ctx->RSSetViewports(1, &vp);
            ctx->IASetInputLayout(layout);
            UINT stride = sizeof(Vertex), offset = 0; ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx->VSSetShader(vs, NULL, 0); ctx->PSSetShader(ps, NULL, 0);
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->Draw(4, 0);

            swap->Present(1, 0);
            srv->Release(); tex->Release(); res->Release();
            dupl->ReleaseFrame();
        }
    }
    return 0;
}
