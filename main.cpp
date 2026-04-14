#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// THE EXACT TITLE
const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 1. Try to find the window
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) {
        MessageBoxW(NULL, L"Game window NOT found! Check the title exactly.", L"Error", MB_ICONERROR);
        return 0;
    }

    RECT gr; GetClientRect(gameHwnd, &gr);
    int upW = (int)((gr.right - gr.left) * SCALE);
    int upH = (int)((gr.bottom - gr.top) * SCALE);
    int posX = (GetSystemMetrics(SM_CXSCREEN) - upW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - upH) / 2;

    // 2. Create Window (Removed WS_EX_TRANSPARENT for testing so we can see it)
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED, // Removed TRANSPARENT to test visibility
        wc.lpszClassName, L"SharpScaler", WS_POPUP | WS_VISIBLE, posX, posY, upW, upH, NULL, NULL, hInstance, NULL);
    
    // Set opacity to 255 (fully visible)
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    
    // 3. DX11 Setup
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    ID3D11Device* dev; ID3D11DeviceContext* ctx; IDXGISwapChain* sc;
    D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, NULL, &ctx);

    ID3D11Texture2D* bb; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    ID3D11RenderTargetView* rtv; dev->CreateRenderTargetView(bb, NULL, &rtv);
    bb->Release();

    // 4. Loop - Just fill with RED to prove it works
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { DispatchMessage(&msg); }
        
        // CLEAR TO RED
        float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, NULL);
        ctx->ClearRenderTargetView(rtv, red);
        sc->Present(1, 0);

        if (GetAsyncKeyState(VK_ESCAPE)) break;
    }
    return 0;
}
