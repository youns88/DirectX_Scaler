#include <windows.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <string>

#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

const char* shaderSource = R"(
    struct VS_IN { float4 pos : POSITION; float2 uv : TEXCOORD; };
    struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
    PS_IN vs_main(VS_IN input) {
        PS_IN output; output.pos = input.pos; output.uv = input.uv;
        return output;
    }
    Texture2D tex; SamplerState samp;
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
    if (!gameHwnd) {
        MessageBoxW(NULL, L"DOSBox-X Window not found!", L"Error", MB_ICONERROR);
        return 0;
    }

    RECT gr; GetClientRect(gameHwnd, &gr);
    int gw = gr.right - gr.left, gh = gr.bottom - gr.top;
    int upW = (int)(gw * SCALE), upH = (int)(gh * SCALE);

    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT, wc.lpszClassName, L"SharpScaler", 
        WS_POPUP | WS_VISIBLE, (GetSystemMetrics(0)-upW)/2, (GetSystemMetrics(1)-upH)/2, upW, upH, NULL, NULL, hInstance, NULL);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 1; scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1; scd.Windowed = TRUE;

    ID3D10Device* dev; IDXGISwapChain* sc;
    HRESULT hr = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_SDK_VERSION, &scd, &sc, &dev);
    if (FAILED(hr)) {
        MessageBoxW(NULL, L"Failed to create DX10 Device. Your GPU might be too old.", L"Error", MB_ICONERROR);
        return 0;
    }

    ID3D10Texture2D* bb; sc->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&bb);
    ID3D10RenderTargetView* rtv; dev->CreateRenderTargetView(bb, NULL, &rtv); bb->Release();

    ID3D10Texture2D* gameTex;
    D3D10_TEXTURE2D_DESC td = { (UINT)gw, (UINT)gh, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1,0}, D3D10_USAGE_DYNAMIC, D3D10_BIND_SHADER_RESOURCE, D3D10_CPU_ACCESS_WRITE, 0 };
    dev->CreateTexture2D(&td, NULL, &gameTex);
    ID3D10ShaderResourceView* srv; dev->CreateShaderResourceView(gameTex, NULL, &srv);

    ID3DBlob *vsB, *psB;
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "vs_main", "vs_4_0", 0, 0, &vsB, NULL);
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "ps_main", "ps_4_0", 0, 0, &psB, NULL);
    ID3D10VertexShader* vs; ID3D10PixelShader* ps;
    dev->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), &vs);
    dev->CreatePixelShader(psB->GetBufferPointer(), psB->GetBufferSize(), &ps);

    Vertex vertices[] = { {-1,1,0,1,0,0}, {1,1,0,1,1,0}, {-1,-1,0,1,0,1}, {1,-1,0,1,1,1} };
    D3D10_BUFFER_DESC vbd = { sizeof(vertices), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0 };
    D3D10_SUBRESOURCE_DATA vsd = { vertices };
    ID3D10Buffer* vb; dev->CreateBuffer(&vbd, &vsd, &vb);

    D3D10_INPUT_ELEMENT_DESC ied[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D10_INPUT_PER_VERTEX_DATA,0}, {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D10_INPUT_PER_VERTEX_DATA,0} };
    ID3D11InputLayout* il; dev->CreateInputLayout(ied, 2, vsB->GetBufferPointer(), vsB->GetBufferSize(), (ID3D10InputLayout**)&il);

    D3D10_SAMPLER_DESC smpD = {}; smpD.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    smpD.AddressU = smpD.AddressV = smpD.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    ID3D10SamplerState* smp; dev->CreateSamplerState(&smpD, &smp);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, gw, gh);
    SelectObject(hdcMem, hbm);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            if (!IsIconic(gameHwnd)) {
                if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOW);
                POINT pt = {0, 0}; ClientToScreen(gameHwnd, &pt);
                BitBlt(hdcMem, 0, 0, gw, gh, hdcScreen, pt.x, pt.y, SRCCOPY);
                
                D3D10_MAPPED_TEXTURE2D map;
                if (SUCCEEDED(gameTex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &map))) {
                    GetBitmapBits(hbm, gw * gh * 4, map.pData);
                    gameTex->Unmap(0);
                }

                dev->OMSetRenderTargets(1, &rtv, NULL);
                D3D10_VIEWPORT vp = { 0, 0, (UINT)upW, (UINT)upH, 0, 1 };
                dev->RSSetViewports(1, &vp);
                dev->IASetInputLayout((ID3D10InputLayout*)il);
                UINT strd = sizeof(Vertex), off = 0;
                dev->IASetVertexBuffers(0, 1, &vb, &strd, &off);
                dev->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                dev->VSSetShader(vs); dev->PSSetShader(ps);
                dev->PSSetShaderResources(0, 1, &srv); dev->PSSetSamplers(0, 1, &smp);
                dev->Draw(4, 0); sc->Present(1, 0);
            } else {
                if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
                Sleep(200);
            }
        }
    }
    return 0;
}
