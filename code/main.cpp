#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <iostream>

// Link necessary libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

typedef HRESULT(__stdcall* Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present OriginalPresent = nullptr;

ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pContext = nullptr;
ID3D11RenderTargetView* pRenderTargetView = nullptr;

// Hooked Present function
HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!pDevice) {
        // Initialize the Direct3D 11 device and context
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
        pDevice->GetImmediateContext(&pContext);

        // Set up the render target view
        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
        pBackBuffer->Release();
    }

    // Bind the render target view
    pContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);

    // Your rendering code (overlay content)
    // Example: Clear the screen with a semi-transparent color
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.3f };  // RGBA (semi-transparent black)
    pContext->ClearRenderTargetView(pRenderTargetView, clearColor);

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
    IDXGISwapChain* dummySwapChain = nullptr;
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
    if (D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            &dummySwapChain,
            &pDevice,
            &featureLevel,
            &pContext) == S_OK) {
        void** vTable = *reinterpret_cast<void***>(dummySwapChain);
        OriginalPresent = reinterpret_cast<Present>(vTable[8]);  // Index of Present in vtable

        // Create hook
        if (MH_CreateHook((LPVOID)OriginalPresent, HookedPresent, reinterpret_cast<LPVOID*>(&OriginalPresent)) == MH_OK) {
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
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
    if (ulReasonForCall == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}
