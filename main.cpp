#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

const wchar_t* TARGET_TITLE = L"DOSBox-X 2025.12.01: DYNA - 3000 cycles/ms";
const float SCALE = 1.6f;

const char* shaderSource = R"(
    struct VS_IN { float4 pos : POSITION; float2 uv : TEXCOORD; };
    struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
    PS_IN vs_main(VS_IN input) {
        PS_IN output;
        output.pos = input.pos;
        output.uv = input.uv;
        return output;
    }
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    float4 ps_main(PS_IN input) : SV_Target {
        return tex.Sample(samp, input.uv);
    }
)";

struct Vertex { float x, y, z, w, u, v; };

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    HWND gameHwnd = FindWindowW(NULL, TARGET_TITLE);
    if (!gameHwnd) {
        MessageBoxW(NULL, L"Game window not found!", L"Error", MB_ICONERROR);
        return 0;
    }

    RECT gr; GetClientRect(gameHwnd, &gr);
    int upW = (int)((gr.right - gr.left) * SCALE);
    int upH = (int)((gr.bottom - gr.top) * SCALE);
    int posX = (GetSystemMetrics(SM_CXSCREEN) - upW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - upH) / 2;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SharpScaler";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"SharpScaler", WS_POPUP | WS_VISIBLE, posX, posY, upW, upH, NULL, NULL, hInstance, NULL);
    SetWindowDisplayAffinity(hwnd, 0x00000011);

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

    // Shaders
    ID3DBlob *vsB, *psB;
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "vs_main", "vs_4_0", 0, 0, &vsB, NULL);
    D3DCompile(shaderSource, strlen(shaderSource), NULL, NULL, NULL, "ps_main", "ps_4_0", 0, 0, &psB, NULL);
    ID3D11VertexShader* vs; ID3D11PixelShader* ps;
    dev->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), NULL, &vs);
    dev->CreatePixelShader(psB->GetBufferPointer(), psB->GetBufferSize(), NULL, &ps);

    // Quad Geometry
    Vertex v[] = { {-1,1,0,1,0,0}, {1,1,0,1,1,0}, {-1,-1,0,1,0,1}, {1,-1,0,1,1,1} };
    D3D11_BUFFER_DESC bd = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA sd = { v };
    ID3D11Buffer* vb; dev->CreateBuffer(&bd, &sd, &vb);

    D3D11_INPUT_ELEMENT_DESC ied[] = { {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}, {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0} };
    ID3D11InputLayout* il; dev->CreateInputLayout(ied, 2, vsB->GetBufferPointer(), vsB->GetBufferSize(), &il);

    D3D11_SAMPLER_DESC smpD = {};
    smpD.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    smpD.AddressU = smpD.AddressV = smpD.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ID3D11SamplerState* smp; dev->CreateSamplerState(&smpD, &smp);

    // Desktop Duplication
    IDXGIDevice* dxDev; dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxDev);
    IDXGIAdapter* adp; dxDev->GetParent(__uuidof(IDXGIAdapter), (void**)&adp);
    IDXGIOutput* out; adp->EnumOutputs(0, &out);
    IDXGIOutput1* out1; out->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
    IDXGIOutputDuplication* dupl; out1->DuplicateOutput(dev, &dupl);

    MSG msg = {0};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { DispatchMessage(&msg); }
        
        IDXGIResource* res = nullptr; DXGI_OUTDUPL_FRAME_INFO fi;
        if (SUCCEEDED(dupl->AcquireNextFrame(10, &fi, &res))) {
            ID3D11Texture2D* tex; res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
            ID3D11ShaderResourceView* srv; dev->CreateShaderResourceView(tex, NULL, &srv);

            ctx->OMSetRenderTargets(1, &rtv, NULL);
            D3D11_VIEWPORT vp = { 0, 0, (float)upW, (float)upH, 0, 1 };
            ctx->RSSetViewports(1, &vp);
            ctx->IASetInputLayout(il);
            UINT strd = sizeof(Vertex), offs = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &strd, &offs);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx->VSSetShader(vs, NULL, 0);
            ctx->PSSetShader(ps, NULL, 0);
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->PSSetSamplers(0, 1, &smp);
            ctx->Draw(4, 0);
            sc->Present(1, 0);

            srv->Release(); tex->Release(); res->Release();
            dupl->ReleaseFrame();
        }
        if (GetAsyncKeyState(VK_ESCAPE)) break;
    }
    return 0;
}
