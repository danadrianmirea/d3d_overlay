#include <DirectXMath.h> // For DirectX::XMFLOAT4
#include <MinHook.h>
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h> // For D3DCompile function
#include <dxgi.h>
#include <fstream> // For logging errors to a file
#include <iostream>
#include <string>


// Link necessary libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib") // Added to link D3DCompiler library

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

// Function to log error messages to a file
void LogError(const std::string& message) {
  std::ofstream logFile("d3d_overlay_error.log", std::ios::app);
  if (logFile.is_open()) {
    logFile << message << std::endl;
  }
  logFile.close();
}

// Hooked Present function
HRESULT __stdcall HookedPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval,
                                UINT Flags) {
  if (!pDevice) {
    // Initialize the Direct3D 11 device and context
    HRESULT hr =
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void **)&pDevice);
    if (FAILED(hr)) {
      LogError("Failed to get D3D11 device");
      return hr;
    }

    pDevice->GetImmediateContext(&pContext);
    
    // Set up the render target view
    ID3D11Texture2D *pBackBuffer = nullptr;
    hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                               (void **)&pBackBuffer);
    if (FAILED(hr)) {
      LogError("Failed to get back buffer");
      return hr;
    }

    hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                         &pRenderTargetView);
    if (FAILED(hr)) {
      LogError("Failed to create render target view");
      return hr;
    }
    pBackBuffer->Release();

    // Compile the shaders
    ID3DBlob *pVSBlob = nullptr;
    ID3DBlob *pPSBlob = nullptr;
    hr =
        D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr,
                   nullptr, nullptr, "main", "vs_4_0", 0, 0, &pVSBlob, nullptr);
    if (FAILED(hr)) {
      LogError("Failed to compile vertex shader");
      return hr;
    }

    hr =
        D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr,
                   nullptr, nullptr, "main", "ps_4_0", 0, 0, &pPSBlob, nullptr);
    if (FAILED(hr)) {
      LogError("Failed to compile pixel shader");
      return hr;
    }

    // Create the shaders
    hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(),
                                     pVSBlob->GetBufferSize(), nullptr,
                                     &pVertexShader);
    if (FAILED(hr)) {
      LogError("Failed to create vertex shader");
      return hr;
    }

    hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(),
                                    pPSBlob->GetBufferSize(), nullptr,
                                    &pPixelShader);
    if (FAILED(hr)) {
      LogError("Failed to create pixel shader");
      return hr;
    }

    // Create the input layout for the vertex shader
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0}};
    hr = pDevice->CreateInputLayout(layout, ARRAYSIZE(layout),
                                    pVSBlob->GetBufferPointer(),
                                    pVSBlob->GetBufferSize(), &pInputLayout);
    if (FAILED(hr)) {
      LogError("Failed to create input layout");
      return hr;
    }

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
    hr = pDevice->CreateBuffer(&bufferDesc, &initData, &pVertexBuffer);
    if (FAILED(hr)) {
      LogError("Failed to create vertex buffer");
      return hr;
    }
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
DWORD WINAPI MainThread(LPVOID lpParameter) {
  // Initialize MinHook
  if (MH_Initialize() != MH_OK) {
    LogError("Failed to initialize MinHook");
    return 1;
  }

  // Hook the Present function
  IDXGISwapChain *dummySwapChain = nullptr;
  D3D_FEATURE_LEVEL featureLevel;
  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.BufferCount = 1;
  swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.OutputWindow = GetConsoleWindow();
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.Windowed = TRUE;

  D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1
};

  IDXGISwapChain *swapChain;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, ARRAYSIZE(featureLevels),
    D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &pDevice, &featureLevel, &pContext);
  if (FAILED(hr)) {
    std::string msg = "Failed to create device and swap chain: ";
    msg += std::to_string(hr);
    LogError(msg);
    return 1;
  }

  void *presentPtr = nullptr; // use void* for the pointer
  hr = swapChain->QueryInterface(__uuidof(IDXGISwapChain), &presentPtr);
  if (FAILED(hr)) {
    LogError("Failed to query Present method");
    return 1;
  }

  // Hook the Present method
  if (MH_CreateHook(presentPtr, &HookedPresent,
                    reinterpret_cast<void **>(&OriginalPresent)) != MH_OK) {
    LogError("Failed to create hook for Present");
    return 1;
  }

  if (MH_EnableHook(presentPtr) != MH_OK) {
    LogError("Failed to enable hook for Present");
    return 1;
  }

  // Main loop to keep the hook active
  while (true) {
    Sleep(100);
  }

  return 0;
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
    CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
  }
  return TRUE;
}
