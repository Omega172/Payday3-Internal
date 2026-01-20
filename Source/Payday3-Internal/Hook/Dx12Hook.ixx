module;
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

export module Hook.Dx12Hook;
import Utils.Logging;
import Menu;

namespace Dx12Hook
{
	// Hook function typedefs
	typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain3*, UINT, UINT);
	typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	typedef void(__stdcall* ExecuteCommandListsFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
	typedef LRESULT(CALLBACK* WndProcFn)(HWND, UINT, WPARAM, LPARAM);

	// Original function pointers
	static PresentFn oPresent = nullptr;
	static ResizeBuffersFn oResizeBuffers = nullptr;
	static ExecuteCommandListsFn oExecuteCommandLists = nullptr;
	static WndProcFn oWndProc = nullptr;

	// Frame context structure
	struct FrameContext
	{
		ID3D12CommandAllocator* pCommandAllocator = nullptr;
		ID3D12Resource* pBackBuffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = {};
	};

	// D3D12 resources
	static ID3D12Device* g_pd3dDevice = nullptr;
	static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
	static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
	static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
	static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
	static IDXGISwapChain3* g_pSwapChain = nullptr;
	static FrameContext* g_frameContext = nullptr;
	static UINT g_numBackBuffers = 0;
	static UINT g_frameIndex = 0;
	static HWND g_hWindow = nullptr;
	static bool g_bInitialized = false;
	static bool g_bShowMenu = true;
	static bool g_bStreamlineDetected = false;
	static bool g_bShuttingDown = false;
	static std::mutex g_initMutex;

	// Forward declarations
	static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	// Create render targets
	static void CreateRenderTarget()
	{
		if (!g_pSwapChain || !g_pd3dDevice || !g_pd3dRtvDescHeap || !g_frameContext || g_numBackBuffers == 0)
			return;

		SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

		// Initialize descriptors
		for (UINT i = 0; i < g_numBackBuffers; i++)
		{
			g_frameContext[i].rtvDescriptor = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}

		// Create render target views
		for (UINT i = 0; i < g_numBackBuffers; i++)
		{
			ID3D12Resource* pBackBuffer = nullptr;
			if (SUCCEEDED(g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer))))
			{
				g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_frameContext[i].rtvDescriptor);
				g_frameContext[i].pBackBuffer = pBackBuffer;
			}
		}
	}

	// Cleanup render targets
	static void CleanupRenderTarget()
	{
		if (!g_frameContext || g_numBackBuffers == 0)
			return;

		for (UINT i = 0; i < g_numBackBuffers; i++)
		{
			if (g_frameContext[i].pBackBuffer)
			{
				g_frameContext[i].pBackBuffer->Release();
				g_frameContext[i].pBackBuffer = nullptr;
			}
		}
	}

	// Initialize ImGui
	static void InitImGui()
	{
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		
		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(g_hWindow);
		ImGui_ImplDX12_Init(g_pd3dDevice, g_numBackBuffers,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			g_pd3dSrvDescHeap,
			g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
			g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
	}

	// Hooked WndProc
	static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		// Always allow menu toggle key
		if (uMsg == WM_KEYDOWN && wParam == VK_INSERT)
		{
			g_bShowMenu = !g_bShowMenu;
			return true; // Block INSERT from game
		}

		// Let ImGui handle input first
		if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
			return true;

		// Block all input to game when menu is open
		if (g_bShowMenu)
		{
			// Block mouse and keyboard input
			switch (uMsg)
			{
            default:
				return true; // Block from game
			}
		}

		return CallWindowProcW(oWndProc, hWnd, uMsg, wParam, lParam);
	}

	// Hooked ExecuteCommandLists - captures command queue
	static void __stdcall HookedExecuteCommandLists(ID3D12CommandQueue* pCommandQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
	{
		if (!g_pd3dCommandQueue)
			g_pd3dCommandQueue = pCommandQueue;

		oExecuteCommandLists(pCommandQueue, NumCommandLists, ppCommandLists);
	}

	// Hooked Present
	static HRESULT __stdcall HookedPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		static bool bInit = false;

		if (!bInit)
		{
			std::lock_guard<std::mutex> lock(g_initMutex);
			
			if (bInit)
				return oPresent(pSwapChain, SyncInterval, Flags);

			// Wait for command queue from ExecuteCommandLists
			if (!g_pd3dCommandQueue)
				return oPresent(pSwapChain, SyncInterval, Flags);

			if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&g_pd3dDevice)))
			{
				DXGI_SWAP_CHAIN_DESC desc;
				pSwapChain->GetDesc(&desc);
				g_hWindow = desc.OutputWindow;
				g_numBackBuffers = desc.BufferCount;

				// Create SRV descriptor heap for ImGui
				{
					D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
					heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
					heapDesc.NumDescriptors = 1;
					heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
					if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_pd3dSrvDescHeap))))
					{
						Utils::LogError("Failed to create SRV descriptor heap");
						return oPresent(pSwapChain, SyncInterval, Flags);
					}
				}

				// Create RTV descriptor heap
				{
					D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
					heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
					heapDesc.NumDescriptors = g_numBackBuffers;
					heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
					heapDesc.NodeMask = 1;
					if (FAILED(g_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_pd3dRtvDescHeap))))
					{
						Utils::LogError("Failed to create RTV descriptor heap");
						return oPresent(pSwapChain, SyncInterval, Flags);
					}
				}

				// Create command allocator
				ID3D12CommandAllocator* allocator = nullptr;
				if (FAILED(g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
				{
					Utils::LogError("Failed to create command allocator");
					return oPresent(pSwapChain, SyncInterval, Flags);
				}

				// Create command list
				if (FAILED(g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList))))
				{
					allocator->Release();
					Utils::LogError("Failed to create command list");
					return oPresent(pSwapChain, SyncInterval, Flags);
				}
				g_pd3dCommandList->Close();

				// Create frame contexts
				g_frameContext = new FrameContext[g_numBackBuffers];
				for (UINT i = 0; i < g_numBackBuffers; i++)
				{
					g_frameContext[i].pCommandAllocator = allocator;
				}

				g_pSwapChain = pSwapChain;

				// Create render targets
				CreateRenderTarget();

				// Hook WndProc
				oWndProc = (WndProcFn)SetWindowLongPtrW(g_hWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

				// Initialize ImGui
				InitImGui();

				bInit = true;
				g_bInitialized = true;
				Utils::LogDebug("DirectX 12 hook initialized successfully");
			}
			
			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		// Check if all required objects exist
		if (!g_pd3dCommandQueue || !g_pd3dDevice || !g_frameContext || !g_pd3dSrvDescHeap)
			return oPresent(pSwapChain, SyncInterval, Flags);

	// If shutting down, skip rendering entirely
	if (g_bShuttingDown)
		return oPresent(pSwapChain, SyncInterval, Flags);

	// Start new frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Render ImGui content
	ImGui::GetIO().MouseDrawCursor = g_bShowMenu;
    Menu::PreDraw();
    Menu::Draw(g_bShowMenu);
    Menu::PostDraw();

	// Always end the frame (required by ImGui)
	ImGui::Render();

	// Get current back buffer
	UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
	FrameContext& frameCtx = g_frameContext[backBufferIdx];

	// Reset command allocator
	frameCtx.pCommandAllocator->Reset();

	// Prepare resource barriers
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = frameCtx.pBackBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// Wait for GPU to finish previous work to avoid state conflicts
	ID3D12Fence* pFence = nullptr;
	if (SUCCEEDED(g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence))))
	{
		HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (hEvent)
		{
			g_pd3dCommandQueue->Signal(pFence, 1);
			pFence->SetEventOnCompletion(1, hEvent);
			WaitForSingleObject(hEvent, 100); // Short timeout
			CloseHandle(hEvent);
		}
		pFence->Release();
	}

	// Execute rendering commands
	g_pd3dCommandList->Reset(frameCtx.pCommandAllocator, nullptr);
	g_pd3dCommandList->ResourceBarrier(1, &barrier);
	g_pd3dCommandList->OMSetRenderTargets(1, &frameCtx.rtvDescriptor, FALSE, nullptr);
	g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

	// Render ImGui draw data
	ImDrawData* pDrawData = ImGui::GetDrawData();
	if (pDrawData && pDrawData->Valid)
		ImGui_ImplDX12_RenderDrawData(pDrawData, g_pd3dCommandList);
		g_pd3dCommandList->ResourceBarrier(1, &barrier);
		g_pd3dCommandList->Close();

		// Execute command list
		g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

		return oPresent(pSwapChain, SyncInterval, Flags);
	}

	// Hooked ResizeBuffers
	static HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
	{
		if (!g_pd3dDevice || !g_pSwapChain)
			return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

		if (g_bInitialized)
		{
			ImGui_ImplDX12_InvalidateDeviceObjects();
			CleanupRenderTarget();
		}

		g_numBackBuffers = BufferCount;

		HRESULT result = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

		if (SUCCEEDED(result) && g_bInitialized)
		{
			CreateRenderTarget();
			ImGui_ImplDX12_CreateDeviceObjects();
		}

		return result;
	}

	// Get D3D12 function addresses via vtable
	static bool GetD3D12Addresses(void** pPresentAddr, void** pResizeBuffersAddr, void** pExecuteCommandListsAddr)
	{
		WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0L, 0L,
			GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX12Temp", nullptr };
		
		if (!RegisterClassExW(&wc))
			return false;

		HWND hWnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
		if (!hWnd)
		{
			UnregisterClassW(wc.lpszClassName, wc.hInstance);
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Width = 1;
		swapChainDesc.Height = 1;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		ID3D12Device* pTempDevice = nullptr;
		ID3D12CommandQueue* pTempQueue = nullptr;
		IDXGISwapChain1* pTempSwapChain = nullptr;
		IDXGIFactory4* pFactory = nullptr;
		bool bSuccess = false;

		if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pTempDevice))))
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

			if (SUCCEEDED(pTempDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pTempQueue))))
			{
				if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory))))
				{
					if (SUCCEEDED(pFactory->CreateSwapChainForHwnd(pTempQueue, hWnd, &swapChainDesc, nullptr, nullptr, &pTempSwapChain)))
					{
						// Get vtable addresses
						void** pSwapChainVTable = *reinterpret_cast<void***>(pTempSwapChain);
						void** pCommandQueueVTable = *reinterpret_cast<void***>(pTempQueue);
						
						*pPresentAddr = pSwapChainVTable[8];  // Present
						*pResizeBuffersAddr = pSwapChainVTable[13];  // ResizeBuffers
						*pExecuteCommandListsAddr = pCommandQueueVTable[10];  // ExecuteCommandLists

						bSuccess = true;

						pTempSwapChain->Release();
					}
					pFactory->Release();
				}
				pTempQueue->Release();
			}
			pTempDevice->Release();
		}

		DestroyWindow(hWnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);

		return bSuccess;
	}
}

export namespace Dx12Hook
{
	bool Initialize()
	{
		Utils::LogDebug("Initializing DirectX 12 hook...");

		// Wait for game to load D3D12
		std::this_thread::sleep_for(std::chrono::seconds(2));

		void* pPresentAddr = nullptr;
		void* pResizeBuffersAddr = nullptr;
		void* pExecuteCommandListsAddr = nullptr;

		if (!GetD3D12Addresses(&pPresentAddr, &pResizeBuffersAddr, &pExecuteCommandListsAddr))
		{
			Utils::LogError("Failed to get DirectX 12 function addresses");
			return false;
		}

		Utils::LogDebug("Got Present address: " + std::to_string(reinterpret_cast<uint64_t>(pPresentAddr)));
		Utils::LogDebug("Got ResizeBuffers address: " + std::to_string(reinterpret_cast<uint64_t>(pResizeBuffersAddr)));
		Utils::LogDebug("Got ExecuteCommandLists address: " + std::to_string(reinterpret_cast<uint64_t>(pExecuteCommandListsAddr)));

		// Create hooks
		MH_STATUS status = MH_CreateHook(pExecuteCommandListsAddr, (LPVOID)&HookedExecuteCommandLists, reinterpret_cast<void**>(&oExecuteCommandLists));
		if (status != MH_OK)
		{
			Utils::LogHook("ExecuteCommandLists", status);
			return false;
		}

		status = MH_CreateHook(pPresentAddr, (LPVOID)&HookedPresent, reinterpret_cast<void**>(&oPresent));
		if (status != MH_OK)
		{
			Utils::LogHook("Present", status);
			return false;
		}

		status = MH_CreateHook(pResizeBuffersAddr, (LPVOID)&HookedResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers));
		if (status != MH_OK)
		{
			Utils::LogHook("ResizeBuffers", status);
			return false;
		}

		// Enable hooks
		status = MH_EnableHook(pExecuteCommandListsAddr);
		if (status != MH_OK)
		{
			Utils::LogHook("ExecuteCommandLists Enable", status);
			return false;
		}

		status = MH_EnableHook(pPresentAddr);
		if (status != MH_OK)
		{
			Utils::LogHook("Present Enable", status);
			return false;
		}

		status = MH_EnableHook(pResizeBuffersAddr);
		if (status != MH_OK)
		{
			Utils::LogHook("ResizeBuffers Enable", status);
			return false;
		}

		Utils::LogDebug("DirectX 12 hook initialization complete");
		return true;
	}

	void Shutdown()
	{
		g_bShuttingDown = true;
		Sleep(100); // Give Present a chance to see shutdown flag

		if (g_bInitialized)
		{
			if (ImGui::GetCurrentContext())
			{
				ImGui_ImplDX12_Shutdown();
				ImGui_ImplWin32_Shutdown();
				ImGui::DestroyContext();
			}

			if (g_hWindow && oWndProc)
				SetWindowLongPtrW(g_hWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);

			CleanupRenderTarget();

			if (g_frameContext)
			{
				for (UINT i = 0; i < g_numBackBuffers; i++)
				{
					if (g_frameContext[i].pCommandAllocator)
						g_frameContext[i].pCommandAllocator->Release();
				}
				delete[] g_frameContext;
			}

			if (g_pd3dCommandList)
				g_pd3dCommandList->Release();
			if (g_pd3dSrvDescHeap)
				g_pd3dSrvDescHeap->Release();
			if (g_pd3dRtvDescHeap)
				g_pd3dRtvDescHeap->Release();

			g_bInitialized = false;
		}

		MH_DisableHook(MH_ALL_HOOKS);
		Utils::LogDebug("DirectX 12 hook shutdown complete");
	}

	bool IsInitialized()
	{
		return g_bInitialized;
	}

	void SetMenuVisible(bool visible)
	{
		g_bShowMenu = visible;
	}

	bool IsMenuVisible()
	{
		return g_bShowMenu;
	}
}