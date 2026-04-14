#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

// Simple Vertex Data
struct Vertex { float x, y, z, w, u, v; };

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Check if game exists
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) {
        MessageBoxW(NULL, L"Could not find DOSBox-X window. Please open the game first!", L"Error", MB_ICONERROR);
        return 0;
    }

    RECT gameRect;
    GetClientRect(gameHwnd, &gameRect);
    int upW = (int)((gameRect.right - gameRect.left) * SCALE);
    int upH = (int)((gameRect.bottom - gameRect.top) * SCALE);
    int posX = (GetSystemMetrics(SM_CXSCREEN) - upW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - upH) / 2;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    // Create window
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"SharpScaler", WS_POPUP, posX, posY, upW, upH, NULL, NULL, hInstance, NULL);
    
    // 0x00000001 (WDA_MONITOR) is safer for older Win10 builds than 0x11
    SetWindowDisplayAffinity(hwnd, 0x00000001); 
    ShowWindow(hwnd, SW_SHOW);

    // DX11 Init
    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    ID3D11Device* dev; ID3D11DeviceContext* ctx; IDXGISwapChain* sc;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, NULL, &ctx);
    if (FAILED(hr)) return 0;

    // --- LOOP ---
    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        // If you see a black screen, the loop is running but capture is failing.
        // We will keep it simple: Render loop here.
        sc->Present(1, 0); 
        if (GetAsyncKeyState(VK_ESCAPE)) break;
    }
    return 0;
}
