#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <iostream>

#include <d3d11.h>
#include <d3dcompiler.h>  // For D3DCompile function
#include <DirectXMath.h>  // For DirectX::XMFLOAT4


// Link necessary libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

typedef HRESULT(__stdcall *Present)(IDXGISwapChain *pSwapChain,
                                    UINT SyncInterval, UINT Flags);
Present OriginalPresent = nullptr;

ID3D11Device *pDevice = nullptr;
ID3D11DeviceContext *pContext = nullptr;
ID3D11RenderTargetView *pRenderTargetView = nullptr;
ID3D11VertexShader *pVertexShader = nullptr;
ID3D11PixelShader *pPixelShader = nullptr;
ID3D11Buffer *pVertexBuffer = nullptr;
ID3D11InputLayout *pInputLayout = nullptr;

// Structure for a 2D vertex
struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT4 color;
};

// Shader code (simple color shader)
const char *vertexShaderSource = R"(
    struct VS_INPUT {
        float3 position : POSITION;
        float4 color : COLOR;
    };
    struct PS_INPUT {
        float4 position : SV_POSITION;
        float4 color : COLOR;
    };
    PS_INPUT main(VS_INPUT input) {
        PS_INPUT output;
        output.position = float4(input.position, 1.0f);
        output.color = input.color;
        return output;
    }
)";

const char *pixelShaderSource = R"(
    struct PS_INPUT {
        float4 position : SV_POSITION;
        float4 color : COLOR;
    };
    float4 main(PS_INPUT input) : SV_Target {
        return input.color;
    }
)";

// Hooked Present function
HRESULT __stdcall HookedPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval,
                                UINT Flags) {
  if (!pDevice) {
    // Initialize the Direct3D 11 device and context
    pSwapChain->GetDevice(__uuidof(ID3D11Device), (void **)&pDevice);
    pDevice->GetImmediateContext(&pContext);

    // Set up the render target view
    ID3D11Texture2D *pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&pBackBuffer);
    pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
    pBackBuffer->Release();

    // Compile the shaders
    ID3DBlob *pVSBlob = nullptr;
    ID3DBlob *pPSBlob = nullptr;
    D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr,
               nullptr, "main", "vs_4_0", 0, 0, &pVSBlob, nullptr);
    D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr, nullptr,
               nullptr, "main", "ps_4_0", 0, 0, &pPSBlob, nullptr);

    // Create the shaders
    pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(),
                                pVSBlob->GetBufferSize(), nullptr,
                                &pVertexShader);
    pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(),
                               pPSBlob->GetBufferSize(), nullptr,
                               &pPixelShader);

    // Create the input layout for the vertex shader
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0}};
    pDevice->CreateInputLayout(layout, ARRAYSIZE(layout),
                               pVSBlob->GetBufferPointer(),
                               pVSBlob->GetBufferSize(), &pInputLayout);

    // Create the vertex buffer for the rectangle (centered at screen)
    Vertex vertices[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.3f}},
                         {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.3f}},
                         {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.3f}},
                         {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.3f}}};
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.ByteWidth = sizeof(vertices);
    D3D11_SUBRESOURCE_DATA initData = {vertices};
    pDevice->CreateBuffer(&bufferDesc, &initData, &pVertexBuffer);
  }

  // Bind the render target view
  pContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

  // Set the shaders and input layout
  pContext->IASetInputLayout(pInputLayout);
  pContext->VSSetShader(pVertexShader, nullptr, 0);
  pContext->PSSetShader(pPixelShader, nullptr, 0);

  // Set up the viewport (entire screen)
  D3D11_VIEWPORT viewport = {0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
  pContext->RSSetViewports(1, &viewport);

  // Bind the vertex buffer
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
  pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  // Draw the rectangle
  pContext->Draw(4, 0);

  // Optionally, add ImGui or custom drawing here

  // Call the original Present function
  return OriginalPresent(pSwapChain, SyncInterval, Flags);
}

// Entry point for the DLL
DWORD WINAPI MainThread(HMODULE hModule) {
  // Initialize MinHook
  if (MH_Initialize() != MH_OK) {
    return 1;
  }

  // Hook the Present function
  IDXGISwapChain *dummySwapChain = nullptr;
  D3D_FEATURE_LEVEL featureLevel;
  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.BufferCount = 1;
  swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferDesc.Width = 1;
  swapChainDesc.BufferDesc.Height = 1;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.OutputWindow = GetConsoleWindow();
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.Windowed = TRUE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  // Create a dummy device and swap chain
  if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    0, nullptr, 0, D3D11_SDK_VERSION,
                                    &swapChainDesc, &dummySwapChain, &pDevice,
                                    &featureLevel, &pContext) == S_OK) {
    void **vTable = *reinterpret_cast<void ***>(dummySwapChain);
    OriginalPresent =
        reinterpret_cast<Present>(vTable[8]); // Index of Present in vtable

    // Create hook
    if (MH_CreateHook((LPVOID)OriginalPresent, HookedPresent,
                      reinterpret_cast<LPVOID *>(&OriginalPresent)) == MH_OK) {
      MH_EnableHook((LPVOID)OriginalPresent);
    }

    // Clean up dummy device
    dummySwapChain->Release();
    pDevice->Release();
    pContext->Release();
  }

  // Keep the thread running
  while (!GetAsyncKeyState(VK_END)) {
    Sleep(100);
  }

  // Unhook and cleanup
  MH_DisableHook((LPVOID)OriginalPresent);
  MH_Uninitialize();
  FreeLibraryAndExitThread(hModule, 0);
  return 0;
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall,
                      LPVOID lpReserved) {
  if (ulReasonForCall == DLL_PROCESS_ATTACH) {
    CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0,
                 nullptr);
  }
  return TRUE;
}
