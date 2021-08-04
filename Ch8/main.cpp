#include <Windows.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <functional>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include "DirectXTex.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

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
ID3D12Fence* _fence = nullptr;
UINT64 _fenceVal = 0;
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

ID3D12DescriptorHeap* texDescHeap = nullptr;
ID3D12DescriptorHeap* matrixDescHeap = nullptr;
ID3D12Resource* uploadaBuff = nullptr;
ID3D12Resource* constBuff = nullptr;
ID3D12Resource* materialBuff = nullptr;
ID3D12DescriptorHeap* materialDescHeap = nullptr;
std::vector<ID3D12Resource*> textureResources;
std::vector<ID3D12Resource*> sphTextureResources;
std::vector<ID3D12Resource*> spaTextureResources;
std::vector<ID3D12Resource*> toonResources;

std::map<std::string, ID3D12Resource*> _resourceTable;


//std::string strModelPath = "../Model/Hatsune_Miku.pmd";
std::string strModelPath = "../Model/hatune_miku_metal.pmd";
//std::string strModelPath = "../Model/ruka.pmd";
//std::string strModelPath = "../Model/MEIKO.pmd";
//std::string strModelPath = "../Model/haku.pmd";


struct MaterialForHlsl
{
	XMFLOAT3 diffuse;
	float alpha;
	XMFLOAT3 specular;
	float specularity;	
	XMFLOAT3 ambient;
};

struct otherMaterialInfo
{
	char texFilePath[20];
	unsigned char toonIdx;
	unsigned char edgeFlg;
};

struct Material
{
	unsigned int indicesNum;
	MaterialForHlsl material;
	otherMaterialInfo otherInfo;
};
std::vector<Material> materials;




struct SceneMatrix {
	XMMATRIX world;
	XMMATRIX view;
	XMMATRIX proj;
	XMFLOAT3 eye;
};

SceneMatrix* mapMatrix;

XMMATRIX worldMat;
XMMATRIX viewMat;
XMMATRIX projMat;

float angle = 0.0f;
unsigned int vertNum = 0;
unsigned int indicesNum = 0;
ID3D12Resource* depthBuffer = nullptr;
ID3D12DescriptorHeap* dsvHeap = nullptr;

HRESULT result;

void EnableDebugLayer()
{
	ID3D12Debug* debugLayer = nullptr;
	HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugLayer));

	debugLayer->EnableDebugLayer();
	debugLayer->Release();
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


void wait()
{
	_cmdQueue->Signal(_fence, ++_fenceVal);
	while (_fence->GetCompletedValue() != _fenceVal)
	{
		HANDLE event = CreateEvent(nullptr, false, false, nullptr);
		_fence->SetEventOnCompletion(_fenceVal, event);
		WaitForSingleObject(event, INFINITE);
	}
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

	// Create Fence		
	result = _dev->CreateFence(_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
}


size_t AlignmentedSize(size_t size, size_t alignment)
{
	return size + alignment - size % alignment;
}


std::string GetExtension(const std::string& path)
{
	int idx = path.rfind(".");
	return path.substr(idx + 1, path.length() - idx - 1);
}


std::pair<std::string, std::string> SplitFileName(const std::string& path, const char splitter = '*')
{
	int idx = path.find(splitter);
	std::pair<std::string, std::string> ret;
	ret.first = path.substr(0, idx);
	ret.second = path.substr(idx+1, path.length() -idx -1);
	return ret;
}


std::string getSphOrSpa(std::string filename, std::string extention)
{
	if (GetExtension(filename) == extention)
	{
		return filename;
	}
	else
	{
		return "";
	}
}


std::string GetTexturePathFromModelAndTexPath(const std::string& modelPath, const char* texPath)
{
	int pathIndex1 = modelPath.rfind('/');
	int pathIndex2 = modelPath.rfind('\\');
	int pathIndex = max(pathIndex1, pathIndex2);

	std::string folderPath = modelPath.substr(0, modelPath.rfind('/'));
	return folderPath + "/" + texPath;
}


std::wstring GetWideStringFromString(const std::string& str)
{
	unsigned int num1 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);

	std::wstring wstr;
	wstr.resize(num1);
	unsigned int num2 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, &wstr[0], num1);
	assert(num1 == num2);
	return wstr;
}


ID3D12Resource* LoadTextureFromFile(std::string& texPath)
{

	auto it = _resourceTable.find(texPath);
	if (it != _resourceTable.end())
	{
		return it->second;
	}


	using LoadLambda_t = std::function<HRESULT(const std::wstring& path, TexMetadata*, ScratchImage&)>;
	std::map<std::string, LoadLambda_t> loadLambdaTable;

	loadLambdaTable["sph"]
		= loadLambdaTable["spa"]
		= loadLambdaTable["bmp"]
		= loadLambdaTable["png"]
		= loadLambdaTable["jpg"]
		= [](const std::wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT
	{
		return LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, meta, img);
	};

	loadLambdaTable["tga"]
		= [](const std::wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT
	{
		return LoadFromTGAFile(path.c_str(), meta, img);
	};

	loadLambdaTable["dds"]
		= [](const std::wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT
	{
		return LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, meta, img);
	};


	std::wstring wtexpath = GetWideStringFromString(texPath);
	std::string ext = GetExtension(texPath);

	TexMetadata texmeta = {};
	ScratchImage scratchImg = {};
	if (loadLambdaTable[ext])
	{
		result = loadLambdaTable[ext](wtexpath, &texmeta, scratchImg);
	}
	else
	{
		return nullptr;
	}


	if (FAILED(result))
	{
		return nullptr;
	}

	const Image* img = scratchImg.GetImage(0, 0, 0);


	// create heap property
	D3D12_HEAP_PROPERTIES texHeapProp = {};
	texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	// create desc 
	D3D12_RESOURCE_DESC texResDesc = {};
	texResDesc.Format = texmeta.format;
	texResDesc.Width = texmeta.width;
	texResDesc.Height = texmeta.height;
	texResDesc.DepthOrArraySize = texmeta.arraySize;
	texResDesc.SampleDesc.Count = 1;
	texResDesc.SampleDesc.Quality = 0;
	texResDesc.MipLevels = texmeta.mipLevels;
	texResDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(texmeta.dimension);
	texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


	// create tex buffer
	ID3D12Resource* texBuff = nullptr;
	result = _dev->CreateCommittedResource(&texHeapProp, D3D12_HEAP_FLAG_NONE, &texResDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texBuff));
	if (FAILED(result))
	{
		return nullptr;
	}

	result = texBuff->WriteToSubresource(0, nullptr, img->pixels, img->rowPitch, img->slicePitch);
	if (FAILED(result))
	{
		return nullptr;
	}

	_resourceTable[texPath] = texBuff;
	return texBuff;
}


ID3D12Resource* CreateWhiteTexture()
{
	// create heap property
	D3D12_HEAP_PROPERTIES texHeapProp = {};
	texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	// create desc 
	D3D12_RESOURCE_DESC texResDesc = {};
	texResDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texResDesc.Width = 4;
	texResDesc.Height = 4;
	texResDesc.DepthOrArraySize = 1;
	texResDesc.SampleDesc.Count = 1;
	texResDesc.SampleDesc.Quality = 0;
	texResDesc.MipLevels = 1;
	texResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


	// create tex buffer
	ID3D12Resource* whiteTexBuff = nullptr;
	result = _dev->CreateCommittedResource(&texHeapProp, D3D12_HEAP_FLAG_NONE, &texResDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&whiteTexBuff));
	if (FAILED(result))
	{
		return nullptr;
	}

	std::vector<unsigned char> data(4*4*4);
	std::fill(data.begin(), data.end(), 0xff);

	result = whiteTexBuff->WriteToSubresource(0, nullptr, data.data(), 4*4, data.size());
	return whiteTexBuff;
}


ID3D12Resource* CreateBlackTexture()
{
	// create heap property
	D3D12_HEAP_PROPERTIES texHeapProp = {};
	texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	// create desc 
	D3D12_RESOURCE_DESC texResDesc = {};
	texResDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texResDesc.Width = 4;
	texResDesc.Height = 4;
	texResDesc.DepthOrArraySize = 1;
	texResDesc.SampleDesc.Count = 1;
	texResDesc.SampleDesc.Quality = 0;
	texResDesc.MipLevels = 1;
	texResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


	// create tex buffer
	ID3D12Resource* whiteTexBuff = nullptr;
	result = _dev->CreateCommittedResource(&texHeapProp, D3D12_HEAP_FLAG_NONE, &texResDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&whiteTexBuff));
	if (FAILED(result))
	{
		return nullptr;
	}

	std::vector<unsigned char> data(4 * 4 * 4);
	std::fill(data.begin(), data.end(), 0x00);

	result = whiteTexBuff->WriteToSubresource(0, nullptr, data.data(), 4 * 4, data.size());
	return whiteTexBuff;
}


ID3D12Resource* CreateGrayGradationTexture()
{
	// create heap property
	D3D12_HEAP_PROPERTIES texHeapProp = {};
	texHeapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
	texHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	texHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	texHeapProp.CreationNodeMask = 0;
	texHeapProp.VisibleNodeMask = 0;

	// create desc 
	D3D12_RESOURCE_DESC texResDesc = {};
	texResDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texResDesc.Width = 4;
	texResDesc.Height = 256;
	texResDesc.DepthOrArraySize = 1;
	texResDesc.SampleDesc.Count = 1;
	texResDesc.SampleDesc.Quality = 0;
	texResDesc.MipLevels = 1;
	texResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


	// create tex buffer
	ID3D12Resource* whiteTexBuff = nullptr;
	result = _dev->CreateCommittedResource(&texHeapProp, D3D12_HEAP_FLAG_NONE, &texResDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&whiteTexBuff));
	if (FAILED(result))
	{
		return nullptr;
	}

	std::vector<unsigned int> data(4 * 256);
	auto it = data.begin();
	unsigned int c = 0xff;
	for (; it != data.end(); it += 4)
	{
		unsigned int col = (0xff << 24) | RGB(c, c, c);
		std::fill(it, it+4, col);
		c--;
	}

	result = whiteTexBuff->WriteToSubresource(0, nullptr, data.data(), 4 * sizeof(unsigned int), sizeof(unsigned int) * data.size());
	return whiteTexBuff;
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

	FILE* fp;

	// Creating vertex buffer
	{
		struct PMDHeader
		{
			float version;
			char model_name[20];
			char comment[256];
		};

		char signature[3] = {};
		PMDHeader pmdheader;
		
		
		fopen_s(&fp, strModelPath.c_str(), "rb");
		fread(signature, sizeof(signature), 1, fp);
		fread(&pmdheader, sizeof(pmdheader), 1, fp);

		struct PMDVertex
		{
			XMFLOAT3 pos;
			XMFLOAT3 normal;
			XMFLOAT2 uv;
			unsigned short boneNo[2];
			unsigned char boneWeight;
			unsigned char edgeFlg;
		};

		constexpr size_t pmdvertex_size = 38;

		fread(&vertNum, sizeof(vertNum), 1, fp);
		std::vector<unsigned char> _vertices(vertNum * pmdvertex_size);
		fread(_vertices.data(), _vertices.size(), 1, fp);		

		std::vector<unsigned char> vertices2(vertNum * 40);
		for (int i = 0; i < vertNum; i++)
		{
			memcpy(&vertices2[i * 40], &_vertices[i * pmdvertex_size], pmdvertex_size);
			vertices2[i * 40 + 38] = 1;
			vertices2[i * 40 + 39] = 1;
		}

		fread(&indicesNum, sizeof(indicesNum), 1, fp);
		std::vector<unsigned short> indices(indicesNum);
		fread(indices.data(), indices.size() * sizeof(indices[0]), 1, fp);

		D3D12_HEAP_PROPERTIES heapProp = {};
		heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
		heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc = CD3DX12_RESOURCE_DESC::Buffer(vertices2.size());
		// Create buffer
		result = _dev->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertBuff));
		resDesc.Width = indices.size() * sizeof(indices[0]);
		result = _dev->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&idxBuff));

		// Copy data to the buffer
		// writing to the address of vertMap writs data to GPU
		unsigned char* vertMap = nullptr;
		result = vertBuff->Map(0, nullptr, (void**)&vertMap);
		std::copy(std::begin(vertices2), std::end(vertices2), vertMap);
		vertBuff->Unmap(0, nullptr);

		// vertex buffer view
		vbView.BufferLocation = vertBuff->GetGPUVirtualAddress();
		vbView.SizeInBytes = vertices2.size();
		vbView.StrideInBytes = 40;

		// writing to the address of mappedIdx writs data to GPU
		unsigned short* mappedIdx = nullptr;
		idxBuff->Map(0, nullptr, (void**)&mappedIdx);
		std::copy(std::begin(indices), std::end(indices), mappedIdx);
		idxBuff->Unmap(0, nullptr);

		// index buffer view
		ibView.BufferLocation = idxBuff->GetGPUVirtualAddress();
		ibView.Format = DXGI_FORMAT_R16_UINT;
		ibView.SizeInBytes = indices.size() * sizeof(indices[0]);

	}


	{};

	// Material
	{

#pragma pack(1)
		struct PMDMaterial
		{
			XMFLOAT3 diffuse;
			float alpha;
			float specularity;
			XMFLOAT3 specular;
			XMFLOAT3 ambient;
			unsigned char toonIdx;
			unsigned char edgeFlg;
			unsigned int indeciesNum;
			char texFilePath[20];
		};
#pragma pack()

		unsigned int materialNum;
		fread(&materialNum, sizeof(materialNum), 1, fp);
		
		std::vector<PMDMaterial> pmdMaterials(materialNum);
		fread(pmdMaterials.data(), pmdMaterials.size() * sizeof(PMDMaterial), 1, fp);
		
		fclose(fp);


		materials.resize(pmdMaterials.size());

		for (int i = 0; i < pmdMaterials.size(); i++)
		{
			materials[i].indicesNum = pmdMaterials[i].indeciesNum;
			materials[i].material.diffuse = pmdMaterials[i].diffuse;
			materials[i].material.alpha = pmdMaterials[i].alpha;
			materials[i].material.specular = pmdMaterials[i].specular;
			materials[i].material.specularity = pmdMaterials[i].specularity;
			materials[i].material.ambient = pmdMaterials[i].ambient;
		}


		// create material buffer
		size_t materialBuffSize = sizeof(MaterialForHlsl);
		materialBuffSize = (materialBuffSize + 0xff) & ~0xff;

		D3D12_HEAP_PROPERTIES heapPropForMat = {};
		heapPropForMat = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC resDescMat = {};
		resDescMat = CD3DX12_RESOURCE_DESC::Buffer(materialBuffSize * materialNum);
		result = _dev->CreateCommittedResource(&heapPropForMat, D3D12_HEAP_FLAG_NONE, &resDescMat, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialBuff));

		// write to material buffer
		char* mapMaterial = nullptr;
		result = materialBuff->Map(0, nullptr, (void**)&mapMaterial);
		for (Material& m : materials)
		{
			*((MaterialForHlsl*)mapMaterial) = m.material;
			mapMaterial += materialBuffSize;
		}
		materialBuff->Unmap(0, nullptr);

		// create an array of texture buffers
		{
			textureResources.resize(pmdMaterials.size());
			sphTextureResources.resize(pmdMaterials.size());
			spaTextureResources.resize(pmdMaterials.size());
			toonResources.resize(pmdMaterials.size());

			for (int i=0; i<pmdMaterials.size(); i++)
			{

				std::string toonFilePath = "../toon/";
				char toonFileName[16];
				sprintf_s(toonFileName, "toon%02d.bmp", pmdMaterials[i].toonIdx + 1);
				toonFilePath += toonFileName;

				if (strlen(pmdMaterials[i].texFilePath) == 0)
				{
					textureResources[i] = nullptr;
					sphTextureResources[i] = nullptr;
					spaTextureResources[i] = nullptr;
				}

				std::string texFileName = pmdMaterials[i].texFilePath;
				std::string sphTextureFileName;
				std::string spaTextureFileName;
				if (std::count(texFileName.begin(), texFileName.end(), '*') > 0)
				{
					std::pair<std::string, std::string> namePair = SplitFileName(texFileName);
					if (GetExtension(namePair.first) == "sph" || GetExtension(namePair.first) == "spa")
					{					
						sphTextureFileName = getSphOrSpa(namePair.first, "sph");
						spaTextureFileName = getSphOrSpa(namePair.first, "spa");
						texFileName = namePair.second;
					}
					else
					{
						texFileName = namePair.first;
						sphTextureFileName = getSphOrSpa(namePair.second, "sph");
						spaTextureFileName = getSphOrSpa(namePair.second, "spa");

					}
				}
				else if (std::count(texFileName.begin(), texFileName.end(), '*') == 0)
				{
					if (GetExtension(texFileName) == "sph" || GetExtension(texFileName) == "spa")
					{
						sphTextureFileName = getSphOrSpa(texFileName, "sph");
						spaTextureFileName = getSphOrSpa(texFileName, "spa");
						texFileName = "";					
					}
					else
					{
						texFileName = texFileName;
					}
				}

				std::string texFilePath = GetTexturePathFromModelAndTexPath(strModelPath, texFileName.c_str());
				std::string sphTexFilePath = GetTexturePathFromModelAndTexPath(strModelPath, sphTextureFileName.c_str());
				std::string spaTexFilePath = GetTexturePathFromModelAndTexPath(strModelPath, spaTextureFileName.c_str());
				textureResources[i] = LoadTextureFromFile(texFilePath);
				sphTextureResources[i] = LoadTextureFromFile(sphTexFilePath);
				spaTextureResources[i] = LoadTextureFromFile(spaTexFilePath);
				toonResources[i] = LoadTextureFromFile(toonFilePath);
			}
		}



		// descriptor heap for materials and textures	
		D3D12_DESCRIPTOR_HEAP_DESC matDescHeapDesc = {};
		matDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		matDescHeapDesc.NodeMask = 0;
		matDescHeapDesc.NumDescriptors = materialNum * 5;
		matDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		result = _dev->CreateDescriptorHeap(&matDescHeapDesc, IID_PPV_ARGS(&materialDescHeap));

		D3D12_CONSTANT_BUFFER_VIEW_DESC matCBVDesc = {};
		matCBVDesc.BufferLocation = materialBuff->GetGPUVirtualAddress();
		matCBVDesc.SizeInBytes = materialBuffSize;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE matDescHeapHandle = materialDescHeap->GetCPUDescriptorHandleForHeapStart();
		unsigned int increment = _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		ID3D12Resource* whiteTex = CreateWhiteTexture();
		ID3D12Resource* blackTex = CreateBlackTexture();
		ID3D12Resource* gradientTex = CreateGrayGradationTexture();
		for (int i = 0; i < materialNum; i++)
		{
			// create constant buffer descriptor for matrix
			_dev->CreateConstantBufferView(&matCBVDesc, matDescHeapHandle);
			matDescHeapHandle.ptr += increment;
			matCBVDesc.BufferLocation += materialBuffSize;


			// create descriptr for texture
			if (textureResources[i] == nullptr)
			{
				srvDesc.Format = whiteTex->GetDesc().Format;
				_dev->CreateShaderResourceView(whiteTex, &srvDesc, matDescHeapHandle);
			}
			else
			{
				srvDesc.Format = textureResources[i]->GetDesc().Format;
				_dev->CreateShaderResourceView(textureResources[i], &srvDesc, matDescHeapHandle);
			}			
			matDescHeapHandle.ptr += increment;


			// create descriptor for sphrical texture	
			if (sphTextureResources[i] == nullptr)
			{
				srvDesc.Format = whiteTex->GetDesc().Format;
				_dev->CreateShaderResourceView(whiteTex, &srvDesc, matDescHeapHandle);
			}
			else
			{				
				srvDesc.Format = sphTextureResources[i]->GetDesc().Format;
				_dev->CreateShaderResourceView(sphTextureResources[i], &srvDesc, matDescHeapHandle);
			}
			matDescHeapHandle.ptr += increment;


			// create descriptor for spherical texture	
			if (spaTextureResources[i] == nullptr)
			{
				srvDesc.Format = blackTex->GetDesc().Format;
				_dev->CreateShaderResourceView(blackTex, &srvDesc, matDescHeapHandle);
			}
			else
			{
				srvDesc.Format = spaTextureResources[i]->GetDesc().Format;
				_dev->CreateShaderResourceView(spaTextureResources[i], &srvDesc, matDescHeapHandle);
			}
			matDescHeapHandle.ptr += increment;

			// create descriptor for gradient texture	
			if (toonResources[i] == nullptr)
			{
				srvDesc.Format = gradientTex->GetDesc().Format;
				_dev->CreateShaderResourceView(gradientTex, &srvDesc, matDescHeapHandle);
			}
			else
			{
				srvDesc.Format = toonResources[i]->GetDesc().Format;
				_dev->CreateShaderResourceView(toonResources[i], &srvDesc, matDescHeapHandle);
			}
			matDescHeapHandle.ptr += increment;

		}

	}

	{};

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
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,	 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"BONE_NO", 0, DXGI_FORMAT_R16G16_UINT,		 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"WEIGHT", 0, DXGI_FORMAT_R8_UINT,			 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"EDGE_FLG", 0, DXGI_FORMAT_R8_UINT,		 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"GARBAGE", 0, DXGI_FORMAT_R8G8_UINT,		 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};


	{};

	// Craete Matrix Constant Buffer
	{		
		worldMat = XMMatrixRotationY(XM_PIDIV4);
		XMFLOAT3 eye(0, 10, -15);
		XMFLOAT3 target(0, 10, 0);
		XMFLOAT3 up(0, 1, 0);
		viewMat = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&up));
		projMat = XMMatrixPerspectiveFovLH(XM_PIDIV2, static_cast<float>(window_width) / static_cast<float>(window_height), 1.0f, 100.0f);

		D3D12_HEAP_PROPERTIES cbHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC cbResDesc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(SceneMatrix) + 0xff) & ~0xff);
		// Create constant buffer
		_dev->CreateCommittedResource(
			&cbHeapProp,
			D3D12_HEAP_FLAG_NONE,
			&cbResDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&constBuff)
		);

		result = constBuff->Map(0, nullptr, (void**)&mapMatrix);
		mapMatrix->world = worldMat;
		mapMatrix->view = viewMat;
		mapMatrix->proj = projMat;
		mapMatrix->eye = eye;


		// create descriptor heap for CBV
		D3D12_DESCRIPTOR_HEAP_DESC matrixDescHeapDesc = {};
		matrixDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		matrixDescHeapDesc.NodeMask = 0;
		matrixDescHeapDesc.NumDescriptors = 1;
		matrixDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		result = _dev->CreateDescriptorHeap(&matrixDescHeapDesc, IID_PPV_ARGS(&matrixDescHeap));

		// create view (descriptor?)
		D3D12_CPU_DESCRIPTOR_HANDLE handle = matrixDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = constBuff->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = constBuff->GetDesc().Width;
		_dev->CreateConstantBufferView(&cbvDesc, handle);

	}


	// Create depth buffer
	{
		D3D12_RESOURCE_DESC depthDesc = {};
		depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthDesc.Width = window_width;
		depthDesc.Height = window_height;
		depthDesc.DepthOrArraySize = 1;
		depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_HEAP_PROPERTIES depthHeapProp = {};
		depthHeapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
		depthHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		depthHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_CLEAR_VALUE depthClearValue = {};
		depthClearValue.DepthStencil.Depth = 1.0f;
		depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;

		result = _dev->CreateCommittedResource(&depthHeapProp, D3D12_HEAP_FLAG_NONE, &depthDesc, 
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthBuffer));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		
		result = _dev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		_dev->CreateDepthStencilView(depthBuffer, &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}


	// Create root signature
	{
		// create descriptor range
		D3D12_DESCRIPTOR_RANGE descTblRange[3] = {};

		// Matrix register
		descTblRange[0].NumDescriptors = 1;
		descTblRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		descTblRange[0].BaseShaderRegister = 0;
		descTblRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		// Material register
		descTblRange[1].NumDescriptors = 1;
		descTblRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		descTblRange[1].BaseShaderRegister = 1;
		descTblRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		// Texture register
		descTblRange[2].NumDescriptors = 4;
		descTblRange[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		descTblRange[2].BaseShaderRegister = 0;
		descTblRange[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


		// create root parameter (Descriptor Table)
		D3D12_ROOT_PARAMETER rootParm[2] = {};
		rootParm[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParm[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParm[0].DescriptorTable.pDescriptorRanges = descTblRange;
		rootParm[0].DescriptorTable.NumDescriptorRanges = 1;
		rootParm[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParm[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParm[1].DescriptorTable.pDescriptorRanges = &descTblRange[1];
		rootParm[1].DescriptorTable.NumDescriptorRanges = 2;


		// create sampler
		D3D12_STATIC_SAMPLER_DESC samplerDesc[2] = {};
		samplerDesc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplerDesc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // いつのまにかD3D12_FILTER_MIN_MAG_MIP_LINEARから変わっている。
		samplerDesc[0].MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc[0].MinLOD = 0.0f;
		samplerDesc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		samplerDesc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplerDesc[0].ShaderRegister = 0;
		samplerDesc[1] = samplerDesc[0];
		samplerDesc[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc[1].ShaderRegister = 1;


		// set up root signature
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.pParameters = rootParm;
		rootSignatureDesc.NumParameters = 2;
		rootSignatureDesc.pStaticSamplers = samplerDesc;
		rootSignatureDesc.NumStaticSamplers = 2;
		ID3DBlob* rootSigBlob = nullptr;
		result = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootSigBlob, &_errorBlob);
		result = _dev->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
	}

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
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;
		psoDesc.DepthStencilState.DepthEnable = true;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

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

	// Create descriptor heap for RTV
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NodeMask = 0;
		heapDesc.NumDescriptors = 2;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		result = _dev->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeaps));
	}

	// create swap chain and rtv descriptor	
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	DXGI_SWAP_CHAIN_DESC swcDesc = {};
	result = _swapchain->GetDesc(&swcDesc);
	vector<ID3D12Resource*> _backBuffers(swcDesc.BufferCount);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeaps->GetCPUDescriptorHandleForHeapStart();
	for (int idx = 0; idx < swcDesc.BufferCount; ++idx)
	{
		result = _swapchain->GetBuffer(idx, IID_PPV_ARGS(&_backBuffers[idx]));
		_dev->CreateRenderTargetView(_backBuffers[idx], &rtvDesc, handle);
		handle.ptr += _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	

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

		// rotate polygon
		angle += 0.025f;
		worldMat = XMMatrixRotationY(angle);
		mapMatrix->world = worldMat;

		// Add commands
		_cmdList->SetPipelineState(_pso);
		_cmdList->SetGraphicsRootSignature(_rootSignature);

		
		_cmdList->SetDescriptorHeaps(1, &matrixDescHeap); // matrix
		_cmdList->SetGraphicsRootDescriptorTable(0, matrixDescHeap->GetGPUDescriptorHandleForHeapStart()); // matrix	
		_cmdList->SetDescriptorHeaps(1, &materialDescHeap); // material		



		_cmdList->RSSetViewports(1, &viewport);
		_cmdList->RSSetScissorRects(1, &scissorRect);
		_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_cmdList->IASetVertexBuffers(0, 1, &vbView);
		_cmdList->IASetIndexBuffer(&ibView);


		UINT bbIdx = _swapchain->GetCurrentBackBufferIndex();

		// Set Render Target
		D3D12_CPU_DESCRIPTOR_HANDLE rtvH = rtvHeaps->GetCPUDescriptorHandleForHeapStart(); // 前のハンドルを使いまわすのも可。ポインタは最初のアドレスに戻しておく必要あり。
		rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvH = dsvHeap->GetCPUDescriptorHandleForHeapStart();
		_cmdList->OMSetRenderTargets(1, &rtvH, true, &dsvH);


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
		//float clearColor[] = { 0.0f, 0.0f, 0.25f, 1.0f };
		float clearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		_cmdList->ClearRenderTargetView(rtvH, clearColor, 0, nullptr);
		_cmdList->ClearDepthStencilView(dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


		D3D12_GPU_DESCRIPTOR_HANDLE matHandle = materialDescHeap->GetGPUDescriptorHandleForHeapStart();
		unsigned int incrementSize = _dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 5;
		unsigned int idxOffset = 0;
		for (Material m : materials)
		{
			_cmdList->SetGraphicsRootDescriptorTable(1, matHandle);
			_cmdList->DrawIndexedInstanced(m.indicesNum, 1, idxOffset, 0, 0);
			matHandle.ptr += incrementSize;
			idxOffset += m.indicesNum;
		}

		


		// Resource Barrier
		BarrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		BarrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		_cmdList->ResourceBarrier(1, &BarrierDesc);

		// execute command
		_cmdList->Close();
		ID3D12CommandList* cmdlists[] = { _cmdList };
		_cmdQueue->ExecuteCommandLists(1, cmdlists);

		wait();

		_cmdAllocator->Reset();
		_cmdList->Reset(_cmdAllocator, nullptr);
		_swapchain->Present(1, 0);
	}



	UnregisterClass(w.lpszClassName, w.hInstance);
	return 0;
}

