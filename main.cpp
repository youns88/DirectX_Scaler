#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")

// --- Configuration ---
const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

// --- Shaders ---
const char* shaderSource = R"(
    struct VS_INPUT { float4 pos : POSITION; float2 uv : TEXCOORD; };
    struct PS_INPUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
    PS_INPUT vs_main(VS_INPUT input) {
        PS_INPUT output;
        output.pos = input.pos;
        output.uv = input.uv;
        return output;
    }
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    float4 ps_main(PS_INPUT input) : SV_Target {
        return tex.Sample(samp, input.uv);
    }
)";

// Global DX11 Variables
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
IDXGISwapChain* swapChain = nullptr;
ID3D11RenderTargetView* rtv = nullptr;
IDXOutputDuplication* deskDupl = nullptr;

void Cleanup() {
    if (deskDupl) deskDupl->Release();
    if (rtv) rtv->Release();
    if (swapChain) swapChain->Release();
    if (context) context->Release();
    if (device) device->Release();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 1. Find Game and Calculate Centered Position
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) {
        MessageBoxA(NULL, "Game window not found!", "Error", MB_OK);
        return 0;
    }

    RECT gameRect;
    GetClientRect(gameHwnd, &gameRect);
    POINT gameTopLeft = {0, 0};
    ClientToScreen(gameHwnd, &gameTopLeft);

    int upW = (int)((gameRect.right - gameRect.left) * SCALE);
    int upH = (int)((gameRect.bottom - gameRect.top) * SCALE);
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (scrW - upW) / 2;
    int posY = (scrH - upH) / 2;

    // 2. Create Borderless Overlay
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"SharpScaler", WS_POPUP, posX, posY, upW, upH, NULL, NULL, hInstance, NULL);
    
    // Set affinity to exclude from capture (prevent hall of mirrors)
    SetWindowDisplayAffinity(hwnd, 0x00000011); 
    ShowWindow(hwnd, SW_SHOW);

    // 3. Initialize DX11
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 
        D3D11_SDK_VERSION, &scd, &swapChain, &device, NULL, &context);

    ID3D11Texture2D* backBuffer;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
    device->CreateRenderTargetView(backBuffer, NULL, &rtv);
    backBuffer->Release();

    // 4. Initialize Desktop Duplication
    IDXGIDevice* dxgiDevice;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter;
    dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&adapter);
    IDXGIOutput* output;
    adapter->EnumOutputs(0, &output);
    IDXGIOutput1* output1;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    output1->DuplicateOutput(device, &deskDupl);
    
    dxgiDevice->Release(); adapter->Release(); output->Release(); output1->Release();

    // 5. Compile Shaders & Setup Point Sampler (Nearest Neighbor)
    ID3DBlob *vsBlob, *psBlob;
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "vs_main", "vs_4_0", 0, 0, &vsBlob, NULL);
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "ps_main", "ps_4_0", 0, 0, &psBlob, NULL);

    ID3D11VertexShader* vs; ID3D11PixelShader* ps;
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &vs);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &ps);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // THE SHARPNESS SETTING
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11SamplerState* sampler;
    device->CreateSamplerState(&sampDesc, &sampler);

    // 6. Main Loop
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }

        IDXGIResource* desktopRes = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        if (SUCCEEDED(deskDupl->AcquireNextFrame(0, &frameInfo, &desktopRes))) {
            ID3D11Texture2D* acquireTex;
            desktopRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquireTex);
            
            // Create a Shader Resource View from the capture and render
            ID3D11ShaderResourceView* srv;
            device->CreateShaderResourceView(acquireTex, NULL, &srv);

            context->OMSetRenderTargets(1, &rtv, NULL);
            float clearColor[] = {0, 0, 0, 1};
            context->ClearRenderTargetView(rtv, clearColor);
            
            context->VSSetShader(vs, NULL, 0);
            context->PSSetShader(ps, NULL, 0);
            context->PSSetShaderResources(0, 1, &srv);
            context->PSSetSamplers(0, 1, &sampler);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            
            context->Draw(4, 0);
            swapChain->Present(1, 0);

            srv->Release();
            acquireTex->Release();
            desktopRes->Release();
            deskDupl->ReleaseFrame();
        }
        if (GetAsyncKeyState(VK_ESCAPE)) break;
    }

    Cleanup();
    return 0;
}
