#include <Windows.h>
#include <tchar.h>
#include <vector>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifdef _DEBUG
#include <iostream>
#endif

using namespace std;
using namespace DirectX;

const unsigned int window_width = 1280;
const unsigned int window_height = 720;


WNDCLASSEX w = {};
HWND hwnd;

ID3D12Device* _dev = nullptr;
IDXGIFactory6* _dxgiFactory = nullptr;
IDXGISwapChain4* _swapchain = nullptr;
ID3D12CommandAllocator* _cmdAllocator = nullptr;
ID3D12GraphicsCommandList* _cmdList = nullptr;
ID3D12CommandQueue* _cmdQueue = nullptr;
ID3D12DescriptorHeap* rtvHeaps = nullptr;
ID3D12Resource* vertBuff = nullptr;
ID3D12Resource* idxBuff = nullptr;
D3D12_VERTEX_BUFFER_VIEW vbView = {};
D3D12_INDEX_BUFFER_VIEW ibView = {};
ID3DBlob* _vsBlob = nullptr;
ID3DBlob* _psBlob = nullptr;
ID3DBlob* _errorBlob = nullptr;
ID3D12RootSignature* _rootSignature = nullptr;
ID3D12PipelineState* _pso = nullptr;
D3D12_VIEWPORT viewport = {};
D3D12_RECT scissorRect = {};

HRESULT result;

void EnableDebugLayer()
{
	ID3D12Debug* debugLayer = nullptr;
	HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer));

	debugLayer->EnableDebugLayer();
	debugLayer->Release();
}


void init()
{
	HRESULT result;

#ifdef _DEBUG
	EnableDebugLayer();
	result = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&_dxgiFactory));
#else
	result = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
#endif

	// get an adapter
	vector<IDXGIAdapter*> adapters;
	IDXGIAdapter* tmpAdapter = nullptr;
	for (int i = 0; _dxgiFactory->EnumAdapters(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		adapters.push_back(tmpAdapter);
	}
	for (IDXGIAdapter* adpt : adapters)
	{
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc(&adesc);
		wstring strDesc = adesc.Description;

		if (strDesc.find(L"NVIDIA") != std::string::npos)
		{
			tmpAdapter = adpt;
			break;
		}
	}

	// create command allocator and command list
	result = D3D12CreateDevice(tmpAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&_dev));
	result = _dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_cmdAllocator));
	result = _dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator, nullptr, IID_PPV_ARGS(&_cmdList));

	// Create Command Queue
	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.NodeMask = 0;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	result = _dev->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&_cmdQueue));
}


void DebugOutputFormatString(const char* format, ...)
{
#ifdef _DEBUG
	va_list valist;
	va_start(valist, format);
	printf(format, valist);
	va_end(valist);
#endif
}

LRESULT WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_DESTROY)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}


#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif	

	// Windows stuff	
	{

		w.cbSize = sizeof(WNDCLASSEX);
		w.lpfnWndProc = (WNDPROC)WindowProcedure;
		w.lpszClassName = _T("DX12Sample");
		w.hInstance = GetModuleHandle(nullptr);
		RegisterClassEx(&w);

		RECT wrc = { 0, 0, window_width, window_height };
		AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

		hwnd = CreateWindow(w.lpszClassName,
			_T("DX12 test"),
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			wrc.right - wrc.left,
			wrc.bottom - wrc.top,
			nullptr,
			nullptr,
			w.hInstance,
			nullptr);

		ShowWindow(hwnd, SW_SHOW);
	}

	// ---------------------------------------------------------------------------------
	// DirectX stuff
	// ---------------------------------------------------------------------------------

	init();

	// Creating vertex buffer
	{
		XMFLOAT3 vertices[4] =
		{
			{-0.4f, -0.7f, 0.0f},
			{-0.4f,  0.7f, 0.0f},
			{ 0.4f, -0.7f, 0.0f},
			{ 0.4f,  0.7f, 0.0f},
		};

		unsigned short indices[] = {
			0, 1, 2,
			2, 1, 3
		};

		D3D12_HEAP_PROPERTIES heapProp = {};
		heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width = sizeof(vertices);
		resDesc.Height = 1;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count = 1;
		resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		// Create buffer
		result = _dev->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertBuff));
		resDesc.Width = sizeof(indices);
		result = _dev->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&idxBuff));


		// Copy data to the buffer
		// writing to the address of vertMap writs data to GPU
		XMFLOAT3* vertMap = nullptr;
		result = vertBuff->Map(0, nullptr, (void**)&vertMap);
		std::copy(std::begin(vertices), std::end(vertices), vertMap);
		vertBuff->Unmap(0, nullptr);
		// writing to the address of mappedIdx writs data to GPU
		unsigned short* mappedIdx = nullptr;
		idxBuff->Map(0, nullptr, (void**)&mappedIdx);
		std::copy(std::begin(indices), std::end(indices), mappedIdx);
		idxBuff->Unmap(0, nullptr);

		
		// vertex and index buffer view
		vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
		vbView.SizeInBytes = sizeof(vertices);
		vbView.StrideInBytes = sizeof(vertices[0]);

		ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
		ibView.Format = DXGI_FORMAT_R16_UINT;
		ibView.SizeInBytes = sizeof(indices);

	}


	// Compile shaders
	{	
		result = D3DCompileFromFile(
			L"VertexShader.hlsl",
			nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"BasicVS",
			"vs_5_0",
			D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
			0,
			&_vsBlob,
			&_errorBlob);

		// _errorBlob にエラーが入るので、プリントしてエラーを確認する。
		if (FAILED(result))
		{
			std::string errstr;
			errstr.resize(_errorBlob->GetBufferSize());
			std::copy_n((char*)_errorBlob->GetBufferPointer(), _errorBlob->GetBufferSize(), errstr.begin());
			//DebugOutputFormatString();
			OutputDebugStringA(errstr.c_str());
		}

		result = D3DCompileFromFile(
			L"PixelShader.hlsl",
			nullptr,
			D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"BasicPS",
			"ps_5_0",
			D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
			0,
			&_psBlob,
			&_errorBlob);

		// _errorBlob にエラーが入るので、プリントしてエラーを確認する。
		if (FAILED(result))
		{
			std::string errstr;
			errstr.resize(_errorBlob->GetBufferSize());
			std::copy_n((char*)_errorBlob->GetBufferPointer(), _errorBlob->GetBufferSize(), errstr.begin());
			//DebugOutputFormatString();
			OutputDebugStringA(errstr.c_str());
		}
	}


	// Create vertex layout
	D3D12_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};


	// Create swap chain. it needs window handle,
	{
		DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
		swapchainDesc.Width = window_width;
		swapchainDesc.Height = window_height;
		swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapchainDesc.Stereo = false;
		swapchainDesc.SampleDesc.Count = 1;
		swapchainDesc.SampleDesc.Quality = 0;
		swapchainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
		swapchainDesc.BufferCount = 2;
		swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		result = _dxgiFactory->CreateSwapChainForHwnd(_cmdQueue, hwnd, &swapchainDesc, nullptr, nullptr, (IDXGISwapChain1**)&_swapchain);
	}

	
	// Create root signature
	{
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ID3DBlob* rootSigBlob = nullptr;
		result = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &_errorBlob);
		result = _dev->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
	}



	// Create pso 
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = _rootSignature;
		psoDesc.VS.pShaderBytecode = _vsBlob->GetBufferPointer();
		psoDesc.VS.BytecodeLength = _vsBlob->GetBufferSize();
		psoDesc.PS.pShaderBytecode = _psBlob->GetBufferPointer();
		psoDesc.PS.BytecodeLength = _psBlob->GetBufferSize();
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.RasterizerState.MultisampleEnable = false;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.DepthClipEnable = true;
		psoDesc.BlendState.AlphaToCoverageEnable = false;
		psoDesc.BlendState.IndependentBlendEnable = false;
		D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendDesc = {};
		renderTargetBlendDesc.BlendEnable = false;
		renderTargetBlendDesc.LogicOpEnable = false;
		renderTargetBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.BlendState.RenderTarget[0] = renderTargetBlendDesc;
		psoDesc.InputLayout.pInputElementDescs = inputLayoutDesc;
		psoDesc.InputLayout.NumElements = _countof(inputLayoutDesc);
		psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		result = _dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso));
	}


	// Create Viewport and Scissor
	{
		viewport.Width = window_width;
		viewport.Height = window_height;
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.MaxDepth = 1.0f;
		viewport.MinDepth = 0.0f;

		scissorRect.top = 0;
		scissorRect.left = 0;
		scissorRect.right = scissorRect.left + window_width;
		scissorRect.bottom = scissorRect.top + window_height;
	}


	// Create descriptor heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NodeMask = 0;
		heapDesc.NumDescriptors = 2;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
	}


	// create swapchain and rtv descriptor
	DXGI_SWAP_CHAIN_DESC swcDesc = {};
	result = _swapchain->GetDesc(&swcDesc);
	vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	for (int idx = 0; idx < swcDesc.BufferCount; ++idx)
	{
		result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
		_dev->CreateRenderTargetView(_backBuffers[idx], nullptr, handle);
		//handle.ptr += idx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); // これはポインタのアドレスが変わっていない。
		handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// Create Fence
	ID3D12Fence* _fence = nullptr;
	UINT64 _fenceVal = 0;
	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));


	// Loop
	MSG msg = {};
	while (true)
	{
		// Windows stuff
		{
			if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			if (msg.message == WM_QUIT)
			{
				break;
			}
		}


		// ---------------------------------------------------------------------------------
		// DirectX stuff
		// ---------------------------------------------------------------------------------

		// Add commands
		_cmdList->SetPipelineState(_pso);
		_cmdList->SetGraphicsRootSignature(_rootSignature);
		_cmdList->RSSetViewports(1, &viewport);
		_cmdList->RSSetScissorRects(1, &scissorRect);
		_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_cmdList->IASetVertexBuffers(0, 1, &vbView);
		_cmdList->IASetIndexBuffer(&ibView);
		
		
		
		UINT bbIdx = _swapchain->GetCurrentBackBufferIndex();

		// Set Render Target
		D3D12_CPU_DESCRIPTOR_HANDLE rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart(); // 前のハンドルを使いまわすのも可。ポインタは最初のアドレスに戻しておく必要あり。
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		_cmdList->OMSetRenderTargets(1, &rtvH, true, nullptr);

		
		// Resource Barrier		
		D3D12_RESOURCE_BARRIER BarrierDesc = {};
		BarrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		BarrierDesc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		BarrierDesc.Transition.pResource = _backBuffers[bbIdx];
		BarrierDesc.Transition.Subresource = 0;
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		_cmdList->ResourceBarrier(1, &BarrierDesc);


		// Clear
		float clearColor[] = { 0.0f, 0.0f, 0.25f, 1.0f };
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
		_cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);



		// Resource Barrier
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		_cmdList->ResourceBarrier(1, &BarrierDesc);

		// execute command
		_cmdList->Close();
		ID3D12CommandList* cmdlists[] = { _cmdList };
		_cmdQueue->ExecuteCommandLists(1, cmdlists);

		_cmdQueue->Signal(_fence, ++_fenceVal);
		while (_fence->GetCompletedValue() != _fenceVal)
		{
			HANDLE event = CreateEvent(nullptr, false, false, nullptr);
			_fence->SetEventOnCompletion(_fenceVal, event);
			WaitForSingleObject(event, INFINITE);
		}


		_cmdAllocator->Reset();
		_cmdList->Reset(_cmdAllocator, nullptr);
		_swapchain->Present(1, 0);
	}



	UnregisterClass(w.lpszClassName, w.hInstance);
	return 0;
}

