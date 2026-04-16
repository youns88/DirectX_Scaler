#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

const wchar_t* TARGET_TITLE = L"Peggle Deluxe 1.01";
const float SCALE = 1.6f;

// Shader with Bilinear Filtering + GPU Sharpening Pass
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
        float2 dims; tex.GetDimensions(dims.x, dims.y);
        float2 pixel = 1.0 / dims;
        
        // Simple Sharpen Kernel
        float4 center = tex.Sample(samp, input.uv);
        float4 neighbor = tex.Sample(samp, input.uv + float2(pixel.x, 0)) +
                         tex.Sample(samp, input.uv - float2(pixel.x, 0)) +
                         tex.Sample(samp, input.uv + float2(0, pixel.y)) +
                         tex.Sample(samp, input.uv - float2(0, pixel.y));
        
        return center + (center - (neighbor * 0.25)) * 0.4; // 0.4 is sharpen strength
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

    RECT cr; GetClientRect(gameHwnd, &cr);
    int gameW = cr.right - cr.left, gameH = cr.bottom - cr.top;
    int upW = (int)(gameW * SCALE), upH = (int)(gameH * SCALE);

    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
        wc.lpszClassName, L"SharpScaler", WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - upW) / 2, (GetSystemMetrics(SM_CYSCREEN) - upH) / 2, 
        upW, upH, NULL, NULL, hInstance, NULL);

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);

    ID3D11Device* dev; ID3D11DeviceContext* ctx; IDXGISwapChain* swap;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 2; sd.BufferDesc.Width = upW; sd.BufferDesc.Height = upH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &swap, &dev, NULL, &ctx);

    ID3D11RenderTargetView* rtv; ID3D11Texture2D* bb;
    swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    dev->CreateRenderTargetView(bb, NULL, &rtv); bb->Release();

    ID3DBlob *vsB, *psB;
    D3DCompile(shaderSource, strlen(shaderSource), 0, 0, 0, "vs_main", "vs_4_0", 0, 0, &vsB, 0);
    D3DCompile(shaderSource, strlen(shaderSource), 0, 0, 0, "ps_main", "ps_4_0", 0, 0, &psB, 0);
    ID3D11VertexShader* vs; ID3D11PixelShader* ps;
    dev->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), 0, &vs);
    dev->CreatePixelShader(psB->GetBufferPointer(), psB->GetBufferSize(), 0, &ps);

    ID3D11InputLayout* lay;
    D3D11_INPUT_ELEMENT_DESC ied[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}, {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0} };
    dev->CreateInputLayout(ied, 2, vsB->GetBufferPointer(), vsB->GetBufferSize(), &lay);

    Vertex v[] = { {-1,1,0,1,0,0}, {1,1,0,1,1,0}, {-1,-1,0,1,0,1}, {1,-1,0,1,1,1} };
    ID3D11Buffer* vb; D3D11_BUFFER_DESC vbd = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA vsrd = { v }; dev->CreateBuffer(&vbd, &vsrd, &vb);

    ID3D11Texture2D* capTex;
    D3D11_TEXTURE2D_DESC td = { (UINT)gameW, (UINT)gameH, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1,0}, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE };
    dev->CreateTexture2D(&td, NULL, &capTex);
    ID3D11ShaderResourceView* srv; dev->CreateShaderResourceView(capTex, NULL, &srv);

    ID3D11SamplerState* samp;
    D3D11_SAMPLER_DESC smd = {}; smd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&smd, &samp);

    HDC hdcGame = GetDC(gameHwnd);
    HDC hdcMem = CreateCompatibleDC(hdcGame);
    HBITMAP hbm = CreateCompatibleBitmap(hdcGame, gameW, gameH);
    SelectObject(hdcMem, hbm);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

        if (IsIconic(gameHwnd)) { ShowWindow(hwnd, SW_HIDE); Sleep(100); continue; }
        else if (!IsWindowVisible(hwnd)) { ShowWindow(hwnd, SW_SHOW); }

        BitBlt(hdcMem, 0, 0, gameW, gameH, hdcGame, 0, 0, SRCCOPY);
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(capTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            BITMAPINFO bmi = {sizeof(BITMAPINFOHEADER), gameW, -gameH, 1, 32, BI_RGB};
            BYTE* temp = (BYTE*)malloc(gameW * 4 * gameH);
            GetDIBits(hdcMem, hbm, 0, gameH, temp, &bmi, DIB_RGB_COLORS);
            for(int y=0; y<gameH; y++) memcpy((BYTE*)mapped.pData + (y * mapped.RowPitch), temp + (y * gameW * 4), gameW * 4);
            free(temp);
            ctx->Unmap(capTex, 0);
        }

        ctx->OMSetRenderTargets(1, &rtv, NULL);
        float clear[] = {0,0,0,1}; ctx->ClearRenderTargetView(rtv, clear);
        D3D11_VIEWPORT vp = {0, 0, (float)upW, (float)upH, 0, 1}; ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(lay);
        UINT str = sizeof(Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, &vb, &str, &off);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(vs, 0, 0); ctx->PSSetShader(ps, 0, 0);
        ctx->PSSetShaderResources(0, 1, &srv); ctx->PSSetSamplers(0, 1, &samp);
        ctx->Draw(4, 0);
        swap->Present(1, 0);
    }

    DeleteObject(hbm); DeleteDC(hdcMem); ReleaseDC(gameHwnd, hdcGame);
    samp->Release(); capTex->Release(); srv->Release(); vb->Release(); lay->Release(); 
    vs->Release(); ps->Release(); rtv->Release(); swap->Release(); ctx->Release(); dev->Release();
    return 0;
}
