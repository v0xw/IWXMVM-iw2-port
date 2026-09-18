#include "StdInclude.hpp"
#include "D3D9.hpp"

#include "MinHook.h"

#include "Components/CaptureManager.hpp"
#include "Events.hpp"
#include "Graphics/Graphics.hpp"
#include "Utilities/PathUtils.hpp"
#include "Mod.hpp"
#include "UI/UIManager.hpp"
#include "Utilities/HookManager.hpp"
#include "Utilities/ExceptionDiagnostics.hpp"

namespace IWXMVM::D3D9
{
    HWND gameWindowHandle = nullptr;
    void* d3d9DeviceVTable[119];
    void* d3d9SwapChainVTable[10];
    void* d3d9VTable[17];
    IDirect3DDevice9* device = nullptr;
    IDirect3DTexture9* depthTexture = nullptr;
    bool foundInterceptedDepthTexture = false;
    std::uint32_t gameWidth = 0;
    std::uint32_t gameHeight = 0;

    // IW2 only: CoD2's offscreen passes (glow, water, ...) share the scene depth stencil, which
    // would scribble over our intercepted depth texture after the scene has rendered. We give
    // those passes a decoy depth surface instead (see SetRenderTarget_Hook).
    IDirect3DSurface9* intzSurface = nullptr;         // owned ref to level 0 of depthTexture
    IDirect3DSurface9* decoyDepthSurface = nullptr;   // stand-in depth for offscreen passes
    IDirect3DSurface9* gameBackBufferId = nullptr;    // identity for comparison only, not a ref
    bool mainDepthStencilSeen = false;

    // Only the first EndScene between two Presents is the game's frame. Later ones come from overlays that
    // draw their own scene on top (GeForce Experience, RivaTuner, ...); handling those too would capture and
    // grade a backbuffer that already carries our UI, and run the UI twice per frame.
    bool frameHandled = false;

    typedef HRESULT(__stdcall* EndScene_t)(IDirect3DDevice9* pDevice);
    EndScene_t EndScene;
    EndScene_t ReshadeOriginalEndScene;
    typedef HRESULT(__stdcall* Reset_t)(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
    Reset_t Reset;
    typedef HRESULT(__stdcall* Present_t)(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect,
                                          HWND hDestWindowOverride, const RGNDATA* pDirtyRegion, DWORD dwFlags);
    Present_t SwapChainPresent;
    typedef HRESULT(__stdcall* DevicePresent_t)(IDirect3DDevice9* pDevice, const RECT* pSourceRect,
                                                const RECT* pDestRect, HWND hDestWindowOverride,
                                                const RGNDATA* pDirtyRegion);
    DevicePresent_t DevicePresent;
    typedef HRESULT(__stdcall* CreateDevice_t)(IDirect3D9* pInterface, UINT Adapter, D3DDEVTYPE DeviceType,
                                               HWND hFocusWindow, DWORD BehaviorFlags,
                                               D3DPRESENT_PARAMETERS* pPresentationParameters,
                                               IDirect3DDevice9** ppReturnedDeviceInterface);
    CreateDevice_t CreateDevice;
    typedef HRESULT(__stdcall* CreateDepthStencilSurface_t)(IDirect3DDevice9* pDevice, UINT Width, UINT Height,
                                                            D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                                            DWORD MultisampleQuality, BOOL Discard,
                                                            IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle);
    CreateDepthStencilSurface_t CreateDepthStencilSurface;
    typedef HRESULT(__stdcall* SetDepthStencilSurface_t)(IDirect3DDevice9* pDevice, IDirect3DSurface9* pNewZStencil);
    SetDepthStencilSurface_t SetDepthStencilSurface;
    typedef HRESULT(__stdcall* SetRenderTarget_t)(IDirect3DDevice9* pDevice, DWORD RenderTargetIndex,
                                                  IDirect3DSurface9* pRenderTarget);
    SetRenderTarget_t SetRenderTarget;

    std::optional<void*> reshadeEndSceneAddress;
    bool IsReshadePresent()
    {
        return reshadeEndSceneAddress.has_value();
    }

    void ReleaseInterceptedDepthResources()
    {
        if (intzSurface)
        {
            intzSurface->Release();
            intzSurface = nullptr;
        }
        if (decoyDepthSurface)
        {
            decoyDepthSurface->Release();
            decoyDepthSurface = nullptr;
        }
        if (depthTexture)
        {
            depthTexture->Release();
            depthTexture = nullptr;
        }
        gameBackBufferId = nullptr;
        mainDepthStencilSeen = false;
    }

    HRESULT __stdcall CreateDepthStencilSurface_Hook(IDirect3DDevice9* pDevice, UINT Width, UINT Height,
        D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
        DWORD MultisampleQuality, BOOL Discard,
        IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
    {
        if (IsReshadePresent())
        {
			return CreateDepthStencilSurface(pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
        }

        HRESULT hr = D3D_OK;

        LOG_DEBUG("CreateDepthStencilSurface called: {}x{}, format {}, multisample {} (expected game resolution {}x{})",
                  Width, Height, static_cast<int>(Format), static_cast<int>(MultiSample), gameWidth, gameHeight);

        bool intercept;
        if (Mod::GetGameInterface()->GetSupportedFeatures() & Types::Features_SharedDepthStencil)
        {
            // the scene depth stencil is created first; later large ones (the RT-shared depth
            // stencil when MSAA is on) and the 128x128 shadow cookies must not be intercepted
            const bool isLarge = Width >= 512 && Height >= 384;
            intercept = false;
            if (isLarge && !mainDepthStencilSeen)
            {
                mainDepthStencilSeen = true;
                intercept = MultiSample == D3DMULTISAMPLE_NONE;
                if (!intercept)
                {
                    LOG_DEBUG("Scene depth stencil is multisampled; depth-based features unavailable");
                }
            }
        }
        else
        {
            intercept = MultiSample == D3DMULTISAMPLE_NONE && Width == gameWidth && Height == gameHeight;
        }

        if (intercept)
        {
            LOG_DEBUG("Intercepting depth stencil surface creation");

            if (intzSurface)
            {
                intzSurface->Release();
                intzSurface = nullptr;
            }
			if (depthTexture)
			{
				depthTexture->Release();
				depthTexture = nullptr;
			}
			hr = pDevice->CreateTexture(Width, Height, 1, D3DUSAGE_DEPTHSTENCIL, static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')), D3DPOOL_DEFAULT,
				&depthTexture, nullptr);
			if (FAILED(hr) || !depthTexture)
			{
				LOG_ERROR("Failed to create depth texture");
                return CreateDepthStencilSurface(pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
			}

            hr = depthTexture->GetSurfaceLevel(0, ppSurface);
            if (SUCCEEDED(hr))
            {
                intzSurface = *ppSurface;
                intzSurface->AddRef();
                LOG_DEBUG("Successfully changed depth stencil format");
            } else
            {
                LOG_DEBUG("Failed to get surface from depth texture");
                return CreateDepthStencilSurface(pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
            }

            return hr;
        }

        return CreateDepthStencilSurface(pDevice, Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface, pSharedHandle);
    }

    IDirect3DSurface9* GetBackBufferId(IDirect3DDevice9* pDevice)
    {
        if (!gameBackBufferId)
        {
            IDirect3DSurface9* backBuffer = nullptr;
            if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) && backBuffer)
            {
                gameBackBufferId = backBuffer;
                backBuffer->Release();
            }
        }
        return gameBackBufferId;
    }

    void EnsureDecoyDepthSurface(IDirect3DDevice9* pDevice)
    {
        if (decoyDepthSurface || !intzSurface)
        {
            return;
        }

        D3DSURFACE_DESC desc = {};
        intzSurface->GetDesc(&desc);

        // created through the trampoline so it doesn't run through our own hook
        if (FAILED(CreateDepthStencilSurface(pDevice, desc.Width, desc.Height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                             FALSE, &decoyDepthSurface, nullptr)))
        {
            LOG_ERROR("Failed to create decoy depth stencil surface");
            decoyDepthSurface = nullptr;
        }
    }

    HRESULT __stdcall SetRenderTarget_Hook(IDirect3DDevice9* pDevice, DWORD RenderTargetIndex,
                                           IDirect3DSurface9* pRenderTarget)
    {
        HRESULT hr = SetRenderTarget(pDevice, RenderTargetIndex, pRenderTarget);

        if (FAILED(hr) || RenderTargetIndex != 0 || !pRenderTarget || !intzSurface ||
            !(Mod::GetGameInterface()->GetSupportedFeatures() & Types::Features_SharedDepthStencil))
        {
            return hr;
        }

        if (!GetBackBufferId(pDevice))
        {
            return hr;
        }

        IDirect3DSurface9* currentDepth = nullptr;
        pDevice->GetDepthStencilSurface(&currentDepth);

        if (pRenderTarget == gameBackBufferId)
        {
            // Scene rendering: depth has to go into our intercepted texture
            if (currentDepth == decoyDepthSurface && decoyDepthSurface)
            {
                SetDepthStencilSurface(pDevice, intzSurface);
            }
        }
        else if (currentDepth == intzSurface)
        {
            // Offscreen pass (glow, water, ...): keep it from overwriting the scene depth
            EnsureDecoyDepthSurface(pDevice);
            if (decoyDepthSurface)
            {
                SetDepthStencilSurface(pDevice, decoyDepthSurface);
            }
        }

        if (currentDepth)
        {
            currentDepth->Release();
        }

        return hr;
    }

    HRESULT __stdcall CreateDevice_Hook(IDirect3D9* pInterface, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
        DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
        IDirect3DDevice9** ppReturnedDeviceInterface)
    {
        LOG_DEBUG("CreateDevice called with hwnd {0:x}", (std::uintptr_t)pPresentationParameters->hDeviceWindow);

        if (UI::UIManager::Get().IsInitialized())
        {
            GFX::GraphicsManager::Get().Uninitialize();
            UI::UIManager::Get().ShutdownImGui();
        }

		foundInterceptedDepthTexture = false;
        ReleaseInterceptedDepthResources();
        frameHandled = false;

        HRESULT hr = CreateDevice(pInterface, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
            ppReturnedDeviceInterface);
        if (FAILED(hr))
        {
            return hr;
        }

        gameWidth = pPresentationParameters->BackBufferWidth;
        gameHeight = pPresentationParameters->BackBufferHeight;

        device = *ppReturnedDeviceInterface;
        
        UI::UIManager::Get().Initialize(device, pPresentationParameters->hDeviceWindow);
        GFX::GraphicsManager::Get().Initialize();

        return hr;
    }

    bool CheckForOverlays(std::uintptr_t returnAddress)
    {
        static constexpr std::array overlayNames{
            "gameoverlay",  // Steam
            "discord"       // Discord
        };
        static std::array<std::uintptr_t, std::size(overlayNames)> returnAddresses{};

        for (std::size_t i = 0; i < returnAddresses.size(); ++i)
        {
            if (!returnAddresses[i])
            {
                MEMORY_BASIC_INFORMATION mbi;
                ::VirtualQuery(reinterpret_cast<LPCVOID>(returnAddress), &mbi, sizeof(MEMORY_BASIC_INFORMATION));

                char module[1024];
                ::GetModuleFileName(static_cast<HMODULE>(mbi.AllocationBase), module, sizeof(module));

                if (std::string_view{module}.find(overlayNames[i]) != std::string_view::npos)
                {
                    returnAddresses[i] = returnAddress;
                    return true;
                }
            }
            else if (returnAddresses[i] == returnAddress)
            {
                return true;
            }
        }

        return false;
    }

    void LogIgnoredEndSceneCaller(std::uintptr_t returnAddress)
    {
        static std::array<std::uintptr_t, 4> seen{};
        for (auto& entry : seen)
        {
            if (entry == returnAddress)
            {
                return;
            }
            if (entry == 0)
            {
                entry = returnAddress;
                LOG_DEBUG("Ignoring EndScene calls from {} that follow the game's own within a frame",
                          ExceptionDiagnostics::DescribeAddress(returnAddress));
                return;
            }
        }
    }

    bool capturedAlready = false;
    std::size_t reshadeEndSceneCallCount;
    HRESULT __stdcall EndScene_Hook(IDirect3DDevice9* pDevice)
    {
        const std::uintptr_t returnAddress = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if (CheckForOverlays(returnAddress))
        {
            return EndScene(pDevice);
        }

        if (!UI::UIManager::Get().IsInitialized())
        {
            device = pDevice;
            UI::UIManager::Get().Initialize(pDevice);
            GFX::GraphicsManager::Get().Initialize();
        }

        if (frameHandled)
        {
            LogIgnoredEndSceneCaller(returnAddress);
            return EndScene(pDevice);
        }
        frameHandled = true;

        if (Mod::GetGameInterface()->GetGameState() == Types::GameState::InDemo)
        {
            // DOF is applied from the game module's CG_Draw2D hook (between the 3D scene and the
            // 2D pass) so HUD elements stay sharp; the filmtweaks grade the complete frame
            GFX::GraphicsManager::Get().ApplyFilmtweaks();
        }

        capturedAlready = false;
        if (Components::CaptureManager::Get().IsCapturing())
        {
            if (IsReshadePresent())
            {
                if (Components::CaptureManager::Get().MultiPassEnabled() && !Components::CaptureManager::Get().GetCurrentPass().useReshade)
                {
                    capturedAlready = true;
                    if (Components::CaptureManager::Get().IsFramePrepared())
                    {
                        Components::CaptureManager::Get().CaptureFrame();
                    }

                    Components::CaptureManager::Get().PrepareFrame();
                }
            }
            else
            {
                if (Components::CaptureManager::Get().IsFramePrepared())
                {
                    Components::CaptureManager::Get().CaptureFrame();
                }

                Components::CaptureManager::Get().PrepareFrame();
            }
        }

        if (Mod::GetGameInterface()->GetGameState() == Types::GameState::InDemo)
        {
            GFX::GraphicsManager::Get().Render();
        }

        if (!reshadeEndSceneAddress.has_value())
        {
            UI::UIManager::Get().RunImGuiFrame();
        }

        return EndScene(pDevice);
    }

    // This is only called if reshade is present
    HRESULT __stdcall ReshadeOriginalEndScene_Hook(IDirect3DDevice9* pDevice)
    {
        if (reshadeEndSceneCallCount > 0)
        {
            return ReshadeOriginalEndScene(pDevice);
        }

        if (Components::CaptureManager::Get().IsCapturing() && (!Components::CaptureManager::Get().MultiPassEnabled() || Components::CaptureManager::Get().GetCurrentPass().useReshade) && !capturedAlready)
        {
            if (Components::CaptureManager::Get().IsFramePrepared())
            {
                Components::CaptureManager::Get().CaptureFrame();
            }

            Components::CaptureManager::Get().PrepareFrame();
        }
      
        ++reshadeEndSceneCallCount;

        UI::UIManager::Get().RunImGuiFrame();
        return ReshadeOriginalEndScene(pDevice);
    }

    HRESULT __stdcall Reset_Hook(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
    {
        if (pPresentationParameters->BackBufferWidth != 0 && pPresentationParameters->BackBufferHeight != 0)
        {
            gameWidth = pPresentationParameters->BackBufferWidth;
            gameHeight = pPresentationParameters->BackBufferHeight;
        }

        ReleaseInterceptedDepthResources();
        foundInterceptedDepthTexture = false;
        frameHandled = false;

        const bool wasUIInitialized = UI::UIManager::Get().IsInitialized();
        if (wasUIInitialized)
		{
			for (const auto& component : UI::UIManager::Get().GetUIComponents())
			{
				component->Release();
			}

			GFX::GraphicsManager::Get().Uninitialize();
			UI::UIManager::Get().ShutdownImGui();
		}

        HRESULT hr = Reset(pDevice, pPresentationParameters);
        
        if (wasUIInitialized)
        {
            UI::UIManager::Get().Initialize(pDevice);
            GFX::GraphicsManager::Get().Initialize();
        }

        return hr;
    }


    HRESULT __stdcall SetDepthStencilSurface_Hook(IDirect3DDevice9* pDevice, IDirect3DSurface9* pNewZStencil)
    {
        // if the game binds the scene depth stencil while an offscreen render target is
        // active, give it the decoy instead (see SetRenderTarget_Hook)
        if (pNewZStencil && pNewZStencil == intzSurface &&
            (Mod::GetGameInterface()->GetSupportedFeatures() & Types::Features_SharedDepthStencil))
        {
            IDirect3DSurface9* currentRenderTarget = nullptr;
            if (SUCCEEDED(pDevice->GetRenderTarget(0, &currentRenderTarget)) && currentRenderTarget)
            {
                const bool offscreen = GetBackBufferId(pDevice) && currentRenderTarget != gameBackBufferId;
                currentRenderTarget->Release();

                if (offscreen)
                {
                    EnsureDecoyDepthSurface(pDevice);
                    if (decoyDepthSurface)
                    {
                        return SetDepthStencilSurface(pDevice, decoyDepthSurface);
                    }
                }
            }
        }

        HRESULT hr = SetDepthStencilSurface(pDevice, pNewZStencil);
        if (!IsReshadePresent() || foundInterceptedDepthTexture || !pNewZStencil)
        {
            return hr;
        }

        D3DSURFACE_DESC surfaceDesc = {};
        pNewZStencil->GetDesc(&surfaceDesc);

        if (surfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE && surfaceDesc.Format == static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')))
        {
            foundInterceptedDepthTexture = true;
            LOG_DEBUG("Found intercepted depth texture");

            HRESULT result = pNewZStencil->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void**>(&depthTexture));
            if (FAILED(result))
            {
                LOG_ERROR("Failed to get parent texture from intercepted surface");
                return hr;
            }
        }

        return hr;
    }

    HRESULT __stdcall SwapChainPresent_Hook(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect,
                                   HWND hDestWindowOverride, const RGNDATA* pDirtyRegion, DWORD dwFlags)
    {
        reshadeEndSceneCallCount = 0;
        frameHandled = false;
        return SwapChainPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
    }

    HRESULT __stdcall DevicePresent_Hook(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect,
                                         HWND hDestWindowOverride, const RGNDATA* pDirtyRegion)
    {
        // whether IDirect3DDevice9::Present routes through the swap chain's Present hooked above is a d3d9
        // implementation detail, so the per-frame state is reset here as well; doing it twice is harmless
        frameHandled = false;
        return DevicePresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }

    void CheckPresenceReshade()
    {
        auto IsReshadeDllPresent = [](auto dllName) {
            const std::filesystem::path gamePath(PathUtils::GetCurrentGameDirectory());
            const auto reshadePath = gamePath / dllName;
            if (!std::filesystem::exists(reshadePath))
            {
                return false;
            }

            bool found = false;
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (SUCCEEDED(hr))
            {
                IShellItem2* pShellItem;
                hr = SHCreateItemFromParsingName(reshadePath.wstring().c_str(), nullptr, IID_PPV_ARGS(&pShellItem));
                if (SUCCEEDED(hr))
                {
                    LPWSTR fileDesc = nullptr;
                    hr = pShellItem->GetString(PKEY_FileDescription, &fileDesc);
                    if (SUCCEEDED(hr))
                    {
                        if (std::wstring_view(fileDesc).find(L"ReShade"))
                        {
                            found = true;
                        }
                        CoTaskMemFree(fileDesc);
                    }
                    pShellItem->Release();
                }
                CoUninitialize();
            }

            return found;
        };

        bool reshadeFound = IsReshadeDllPresent("d3d9.dll") ||
                            IsReshadeDllPresent("dxgi.dll");

        auto device = Mod::GetGameInterface()->GetGameDevicePtr();
        auto vTable = *reinterpret_cast<void***>(device);
        auto orgEndScene = vTable[42];

        if (reshadeFound && orgEndScene != d3d9DeviceVTable[42])
        {
            LOG_DEBUG("Detected Reshade presence; original EndScene address is {}, Reshade EndScene address is {}.",
                      orgEndScene, d3d9DeviceVTable[42]);

            reshadeEndSceneAddress = std::exchange(d3d9DeviceVTable[42], orgEndScene);
        }
    }

    void FindSwapChain()
    {
        const auto device = Mod::GetGameInterface()->GetGameDevicePtr();

        IDirect3DSwapChain9* pSwapChain = nullptr;
        const HRESULT hr = device->GetSwapChain(0, &pSwapChain);

        if (FAILED(hr) || !pSwapChain)
        {
            throw std::runtime_error("Failed to find D3D9 SwapChain!");
        }
        else
        {
            memcpy(d3d9SwapChainVTable, *(void**)pSwapChain, 10 * sizeof(void*));
            pSwapChain->Release();

            LOG_DEBUG("Found D3D9 SwapChain Present address: {}",
                      ExceptionDiagnostics::DescribeAddress(reinterpret_cast<uintptr_t>(d3d9SwapChainVTable[3])));
        }
    }

    void CreateDummyDevice()
    {
        IDirect3D9* d3dObj = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3dObj)
        {
            throw std::runtime_error("Failed to create D3D object");
        }

        IDirect3DDevice9* dummyDevice = nullptr;
        D3DPRESENT_PARAMETERS d3d_params{};

        // Try to create device - will fail if in fullscreen
        HRESULT result = d3dObj->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, NULL,
                                              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT,
                                              &d3d_params, &dummyDevice);

        // Fail -> death
        if (FAILED(result) || !dummyDevice)
        {
            d3dObj->Release();
            throw std::runtime_error("Failed to create dummy D3D device");
        }

        memcpy(d3d9DeviceVTable, *(void**)dummyDevice, 119 * sizeof(void*));
        memcpy(d3d9VTable, *(void**)d3dObj, 17 * sizeof(void*));

        LOG_DEBUG("Created dummy D3D device");

        CheckPresenceReshade();

        dummyDevice->Release();
        d3dObj->Release();
    }

    void Hook()
    {
        // TODO: move minhook initialization somewhere else
        if (MH_Initialize() != MH_OK)
        {
            throw std::runtime_error("Failed to initialize MinHook");
        }

        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[29], (std::uintptr_t)CreateDepthStencilSurface_Hook,
            (std::uintptr_t*)&CreateDepthStencilSurface);
        HookManager::CreateHook((std::uintptr_t)d3d9VTable[16], (std::uintptr_t)CreateDevice_Hook,
            (std::uintptr_t*)&CreateDevice);
        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[16], (std::uintptr_t)Reset_Hook,
            (std::uintptr_t*)&Reset);
        HookManager::CreateHook((std::uintptr_t)d3d9SwapChainVTable[3], (std::uintptr_t)SwapChainPresent_Hook,
            (std::uintptr_t*)&SwapChainPresent);
        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[17], (std::uintptr_t)DevicePresent_Hook,
            (std::uintptr_t*)&DevicePresent);
        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[42], (std::uintptr_t)EndScene_Hook,
            (std::uintptr_t*)&EndScene);
        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[39], (std::uintptr_t)SetDepthStencilSurface_Hook,
            (std::uintptr_t*)&SetDepthStencilSurface);
        HookManager::CreateHook((std::uintptr_t)d3d9DeviceVTable[37], (std::uintptr_t)SetRenderTarget_Hook,
            (std::uintptr_t*)&SetRenderTarget);
        
        if (reshadeEndSceneAddress.has_value())
        {
            HookManager::CreateHook((std::uintptr_t)reshadeEndSceneAddress.value(),
                                    (std::uintptr_t)ReshadeOriginalEndScene_Hook,
                                    (std::uintptr_t*)&ReshadeOriginalEndScene);
        }
    }

    void Initialize()
    {
        // A d3d9.dll wrapper in the game folder (ReShade, dgVoodoo, DXVK, ...) takes the place of the system
        // runtime, which is the first thing to know about any failure in here.
        if (const auto d3d9Module = GetModuleHandleA("d3d9.dll"))
        {
            char path[MAX_PATH]{};
            GetModuleFileNameA(d3d9Module, path, MAX_PATH);
            LOG_INFO("Direct3D 9 runtime: {}", path);
        }

        FindSwapChain();
        CreateDummyDevice();
        Hook();
        LOG_DEBUG("Hooked D3D9");

        if (!IsReshadePresent())
        {
            LOG_DEBUG("Triggering vid_restart since Reshade is not present");
            Mod::GetGameInterface()->Vid_Restart();
        }
    }

    HWND FindWindowHandle()
    {
        auto* device = GetDevice();
        if (device == nullptr)
        {
            LOG_CRITICAL("Failed to get the game window");
            return nullptr;
        }

        D3DDEVICE_CREATION_PARAMETERS params{};
        device->GetCreationParameters(&params);

        if (params.hFocusWindow == nullptr)
        {
            LOG_CRITICAL("Failed to get the game window");
            return nullptr;
        }

        return gameWindowHandle = params.hFocusWindow;
    }

    IDirect3DDevice9* GetDevice()
    {
        return device;
    }

    IDirect3DTexture9* GetDepthTexture()
    {
        return depthTexture;
    }

    bool CaptureBackBuffer(IDirect3DTexture9* texture)
    {
        auto device = GetDevice();

        IDirect3DSurface9* RenderTarget = NULL;
        auto result = device->GetRenderTarget(0, &RenderTarget);
        if (FAILED(result))
            return false;

        IDirect3DSurface9* textureSurface;
        result = texture->GetSurfaceLevel(0, &textureSurface);
        if (FAILED(result))
        {
            textureSurface->Release();
            RenderTarget->Release();
            return false;
        }

        result = device->StretchRect(RenderTarget, NULL, textureSurface, NULL, D3DTEXF_LINEAR);
        if (FAILED(result))
        {
            textureSurface->Release();
            RenderTarget->Release();
            return false;
        }

        textureSurface->Release();
        RenderTarget->Release();
        return true;
    }

    bool CreateTexture(IDirect3DTexture9*& texture, ImVec2 size)
    {
        if (texture != NULL)
            texture->Release();

        auto device = D3D9::GetDevice();

        auto result = D3DXCreateTexture(device, (UINT)size.x, (UINT)size.y, D3DX_DEFAULT, D3DUSAGE_RENDERTARGET,
                                        D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture);
        if (FAILED(result))
            return false;

        return true;
    }
}  // namespace IWXMVM::D3D9
