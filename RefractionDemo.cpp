#include "RefractionDemo.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <sstream>
#include <fstream>
#include <vector>
#include <assert.h>

constexpr int swapchainBufferCount = 2;

int width, height;
struct {
    DirectX::XMMATRIX proj_inv;
    DirectX::XMVECTOR camera_loc;
} sceneConstants;

Mesh cubeMesh;

ComPtr<IDXGIFactory2> factory;
ComPtr<ID3D12Device5> device;

// Cached GPU sizes for descriptors (used for offsets into descriptor tables)
UINT rtvDescriptorSize;
UINT dsvDescriptorSize;
UINT cbvDescriptorSize; // also SRV and UAV

ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12DescriptorHeap> dsvHeap;
ComPtr<ID3D12DescriptorHeap> srvHeap;

ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent;
volatile UINT64 fenceValue;

ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<ID3D12RootSignature> localRootSignature;
ComPtr<IDXGISwapChain3> swapchain;

ComPtr<ID3D12Resource> depthStencilBuffer;
ComPtr<ID3D12Resource> renderTargets[swapchainBufferCount];

ComPtr<ID3D12Resource> cameraConstantBuffer;
ComPtr<ID3D12PipelineState> pipelineState;

ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList5> commandList;

ComPtr<ID3D12Resource> rtTexture;
ComPtr<ID3D12Resource> blasScratch;
ComPtr<ID3D12Resource> blasResult;
ComPtr<ID3D12Resource> tlasScratch;
ComPtr<ID3D12Resource> tlasResult;
ComPtr<ID3D12Resource> instanceDescs;
ComPtr<ID3D12StateObject> rtPSO;
ComPtr<ID3D12Resource> raygenTable;
ComPtr<ID3D12Resource> hitTable;
ComPtr<ID3D12Resource> missTable;
ComPtr<ID3D12Resource> envMap;

ComPtr<ID3D12DescriptorHeap> descriptorHeap;



void wait_until_finished()
{
    InterlockedIncrement(&fenceValue);
    commandQueue->Signal(fence.Get(), fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
}

void create_upload_buffer(ID3D12Resource** resource, ComPtr<ID3D12Device5>& device, unsigned size)
{
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(resource));
}

void copy_to_buffer(ComPtr<ID3D12Resource>& resource, void* data, unsigned size)
{
    D3D12_RANGE range = {};
    void* p;
    resource->Map(0, &range, &p);
    memcpy(p, data, size);
    resource->Unmap(0, nullptr);
}

bool load_texture(ID3D12Resource** texture, ID3D12GraphicsCommandList* commandList, const char* filename)
{
    int x, y, n;
    float* data = stbi_loadf(filename, &x, &y, &n, 3);

    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32_FLOAT, x, y),
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(texture));
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(*texture, 0, 1);

    ComPtr<ID3D12Resource> uploadBuffer;
    create_upload_buffer(uploadBuffer.GetAddressOf(), device, uploadBufferSize);

    copy_to_buffer(uploadBuffer, data, x * y * n);

    ComPtr<ID3D12GraphicsCommandList> copyList;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&copyList));

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = data;
    textureData.RowPitch = x * 3 * sizeof(float);
    textureData.SlicePitch = textureData.RowPitch * y;
    UpdateSubresources(copyList.Get(), *texture, uploadBuffer.Get(), 0, 0, 1, &textureData);

    copyList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(*texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));
    copyList->Close();
    ID3D12CommandList* const commandLists[] = { copyList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    wait_until_finished();

    stbi_image_free(data);
    return true;
}

void createDevice()
{
    UINT factoryFlags = 0;
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    debug->EnableDebugLayer();

    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    // We can use this to enumerate adapters. For now we just pick the default.
    CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));

    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps = {};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps, sizeof(caps));
    assert(caps.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);

    D3D12_COMMAND_QUEUE_DESC desc;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;
    desc.Priority = 0;
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));

    // Cache these GPU sizes.
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cbvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// Recreate the swapchain and associated stuff like the render targets and
// the depth/stencil buffer and command allocator+list.
void recreateSwapchain(HWND hwnd, int width, int height)
{
    // Allow re-entry, since this will be called every window resize.
    swapchain.Reset();

    DXGI_SWAP_CHAIN_DESC1 desc;
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = swapchainBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapchain1;
    factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapchain1);
    swapchain1.As(&swapchain);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    rtvHeapDesc.NumDescriptors = swapchainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));

    // Allocate a buffer for each render target in the swapchain, storing a
    // view in our RTV descriptor table which we will use to draw.
    CD3DX12_CPU_DESCRIPTOR_HANDLE ptr(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < swapchainBufferCount; i++) {
       
        swapchain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, ptr);
        renderTargets[i]->SetName(L"RenderTarget ");
        ptr.Offset(1, rtvDescriptorSize);
    }

    // Create and clear a texture which will serve as our depth/stencil buffer
    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0u),
        IID_PPV_ARGS(&depthStencilBuffer));
    depthStencilBuffer->SetName(L"Depth/Stencil Buffer");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));


    // Create a descriptor on our DSV heap pointing to this texture.
    device->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void createSignatures()
{
    // The root signature is our interface to the shaders. We are simply describing the data here,
    // we provide the contents in drawFrame each frame.

    // Main root signature
    {
    CD3DX12_ROOT_PARAMETER1 rp[4];
    rp[0].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0));
    rp[1].InitAsShaderResourceView(0);
    rp[2].InitAsConstantBufferView(0);
    rp[3].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1));
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rp), rp, 1, &CD3DX12_STATIC_SAMPLER_DESC(0), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    D3D12SerializeVersionedRootSignature(&rootSigDesc, &blob, nullptr);
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    rootSignature->SetName(L"Global Root Signature");
    }

    // Local root signature (for raytracing)
    {
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC localRootSigDesc;
    localRootSigDesc.Init_1_1(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    localRootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    ComPtr<ID3DBlob> blob;
    D3D12SerializeVersionedRootSignature(&localRootSigDesc, &blob, nullptr);
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&localRootSignature));
    localRootSignature->SetName(L"Local Root Signature");
    }
}

void setupRaytracingAccelerationStructures()
{
    // A top and bottom level acceleration structure must be defined for the
    // geometry. This is essentially a BVH.

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = cubeMesh.raytracingGeometry();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
    bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomLevelInputs.pGeometryDescs = &geometryDesc;
    bottomLevelInputs.NumDescs = 1;
    bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    // Get the size of the resources we need to allocate for the acceleration structures.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
    device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &prebuildInfo);

    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    resourceDesc.Width = prebuildInfo.ScratchDataSizeInBytes;
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&blasScratch));
    resourceDesc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&blasResult));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelDesc = {};
    bottomLevelDesc.Inputs = bottomLevelInputs;
    bottomLevelDesc.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
    bottomLevelDesc.DestAccelerationStructureData = blasResult->GetGPUVirtualAddress();

    commandList->BuildRaytracingAccelerationStructure(&bottomLevelDesc, 0, nullptr);
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(blasResult.Get()));

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.Transform[0][0] = 1.0f;
    instanceDesc.Transform[1][1] = 1.0f;
    instanceDesc.Transform[2][2] = 1.0f;
    instanceDesc.InstanceMask = 1;
    instanceDesc.InstanceID = 0;
    instanceDesc.Flags = 0;
    instanceDesc.InstanceContributionToHitGroupIndex = 0;
    instanceDesc.AccelerationStructure = blasResult->GetGPUVirtualAddress();
    create_upload_buffer(instanceDescs.GetAddressOf(), device, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    copy_to_buffer(instanceDescs, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
    topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.pGeometryDescs = nullptr;
    topLevelInputs.NumDescs = 1;
    topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
    topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    resourceDesc.Width = prebuildInfo.ScratchDataSizeInBytes;
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tlasScratch));
    resourceDesc.Width = prebuildInfo.ResultDataMaxSizeInBytes;
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&tlasResult));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelDesc = {};
    topLevelDesc.Inputs = topLevelInputs;
    topLevelDesc.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();
    topLevelDesc.DestAccelerationStructureData = tlasResult->GetGPUVirtualAddress();

    commandList->BuildRaytracingAccelerationStructure(&topLevelDesc, 0, nullptr);
    tlasResult->SetName(L"TopLevel Acceleration Struct Result");
    tlasScratch->SetName(L"TopLevel Acceleration Struct Scratch");
    blasResult->SetName(L"BottomLevel Acceleration Struct Result");
    blasScratch->SetName(L"BottomLevel Acceleration Struct Scratch");
}

IDxcBlob* rayGenBytecode;
dxc::DxcDllSupport dxcHelper;
IDxcCompiler* compiler;
IDxcLibrary* library;
IDxcIncludeHandler* includeHandler;

void setupRaytracingPipelineStateObjects()
{
    // Compile the shaders as a library. To do this we need to import a DLL
    // since we need a recent HLSL compiler.

    dxcHelper.Initialize();
    dxcHelper.CreateInstance(CLSID_DxcCompiler, &compiler);
    dxcHelper.CreateInstance(CLSID_DxcLibrary, &library);
    library->CreateIncludeHandler(&includeHandler);

    UINT32 codePage = 0;
    IDxcBlobEncoding* shaderText;
    IDxcOperationResult* result;
    library->CreateBlobFromFile(L"../RayTracing.hlsl", &codePage, &shaderText);
    compiler->Compile(shaderText, L"../RayTracing.hlsl", nullptr, L"lib_6_3",
        nullptr, 0, nullptr, 0, includeHandler, &result);
    result->GetResult(&rayGenBytecode);

    HRESULT compilationStatus;
    result->GetStatus(&compilationStatus);
    if (FAILED(compilationStatus)) {
        IDxcBlobEncoding* error;
        result->GetErrorBuffer(&error);
        OutputDebugStringA((char*)error->GetBufferPointer());
        assert(0);
    }

    // The State Object let's us define a bunch of things that DirectX needs to
    // know about our shaders in order to use them.

    CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    auto subobjDXIL = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    subobjDXIL->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE(rayGenBytecode->GetBufferPointer(), rayGenBytecode->GetBufferSize()));

    auto subobjHit = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    subobjHit->SetHitGroupExport(L"HitGroup");
    subobjHit->SetClosestHitShaderImport(L"ClosestHit");
    subobjHit->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    
    auto subobjShaderConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    subobjShaderConfig->Config(sizeof(DirectX::XMFLOAT4)*2, sizeof(DirectX::XMFLOAT2));

    auto subobjLocalSig = stateObjectDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    subobjLocalSig->SetRootSignature(localRootSignature.Get());
    auto assocSubobj = stateObjectDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    assocSubobj->SetSubobjectToAssociate(*subobjLocalSig);
    assocSubobj->AddExport(L"HitGroup");

    auto subobjRootSig = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    subobjRootSig->SetRootSignature(rootSignature.Get());
    
    auto subobjPipeline = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    subobjPipeline->Config(31);

    assert(SUCCEEDED(device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&rtPSO))));
    rtPSO->SetName(L"RayTracing PSO");
}

void createRaytracingTexture()
{
    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&rtTexture));
    rtTexture->SetName(L"RayTracing Texture");
}

void createShaderTables()
{
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    rtPSO.As(&stateObjectProperties);
    void* rayGenId = stateObjectProperties->GetShaderIdentifier(L"RayGen");
    void* missId = stateObjectProperties->GetShaderIdentifier(L"Miss");
    void* hitGroupId = stateObjectProperties->GetShaderIdentifier(L"HitGroup");

    D3D12_RANGE range = { 0,0 };
    char* p;

    create_upload_buffer(raygenTable.GetAddressOf(), device, 64);
    raygenTable->SetName(L"RayGen Table");
    raygenTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], rayGenId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    raygenTable->Unmap(0, nullptr);

    create_upload_buffer(hitTable.GetAddressOf(), device, 64);
    hitTable->SetName(L"Hit Table");
    hitTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], hitGroupId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    hitTable->Unmap(0, nullptr);

    create_upload_buffer(missTable.GetAddressOf(), device, 64);
    missTable->SetName(L"Miss Table");
    missTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], missId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    missTable->Unmap(0, nullptr);
}

void createDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc;
    srvDesc.NodeMask = 0;
    srvDesc.NumDescriptors = 4;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap));

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(rtTexture.Get(), nullptr, &desc, srvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = cubeMesh.indices.size();
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        desc.Buffer.StructureByteStride = sizeof(uint32_t);
        device->CreateShaderResourceView(cubeMesh.ib.Get(), &desc, { srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + cbvDescriptorSize });
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = cubeMesh.verts.size();
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        desc.Buffer.StructureByteStride = sizeof(Vertex);
        device->CreateShaderResourceView(cubeMesh.vb.Get(), &desc, { srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + 2*cbvDescriptorSize });
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.PlaneSlice = 0;
        desc.Texture2D.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(envMap.Get(), &desc, { srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + 3*cbvDescriptorSize });
    }
}

void RefractionDemo::initialize(HWND hWnd, int width_, int height_)
{
    width = width_;
    height = height_;

    createDevice();
    // Create a synchronization object which we will use to ensure the GPU is done after swapping buffers.
    // This is temporary and a bad way of doing things. We should really just use separate command lists
    // for each back buffer so we can let the GPU continue on ahead of the CPU.
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    // We should be creating one of these per RTV, for now we just create one.
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    load_texture(envMap.GetAddressOf(), commandList.Get(), "../envMap.hdr");
    envMap->SetName(L"Environment Map Texture");
    
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));


    unsigned size = (sizeof(sceneConstants) + 255u) & ~255u;
    create_upload_buffer(cameraConstantBuffer.GetAddressOf(), device, size);

    createSignatures();
    cubeMesh.load("../shell.obj");
    cubeMesh.upload(device);

    
    setupRaytracingAccelerationStructures();
    setupRaytracingPipelineStateObjects();
    createRaytracingTexture();
    createShaderTables();

    recreateSwapchain(hWnd, width, height);
    createDescriptorHeap();

    commandList->Close();
    ID3D12CommandList* const commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    wait_until_finished();
}

static float angle = 0.01f;

void RefractionDemo::drawFrame()
{
    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(52.0f / 180.0 * 3.1415, 1.333, 1.0f, 125.0f);
    sceneConstants.camera_loc = { 5 * cosf(angle),0,5 * sinf(angle),1.0 };
    DirectX::XMMATRIX world = DirectX::XMMatrixTranslationFromVector(sceneConstants.camera_loc);
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH({ cosf(-angle),0.0,sinf(-angle),1.0 }, { 0.0,0.0,0.0,1.0 }, { 0.0,1.0,0.0,0.0 });
    DirectX::XMMATRIX projView = proj * world * view;

    sceneConstants.proj_inv = DirectX::XMMatrixInverse(nullptr, projView);
    copy_to_buffer(cameraConstantBuffer, &sceneConstants, sizeof(sceneConstants));
    angle += 0.01f;

    int frameIdx = swapchain->GetCurrentBackBufferIndex();

    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), nullptr);
    commandList->SetDescriptorHeaps(1, srvHeap.GetAddressOf());
    commandList->SetComputeRootSignature(rootSignature.Get());
    commandList->SetComputeRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->SetComputeRootShaderResourceView(1, tlasResult->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(2, cameraConstantBuffer->GetGPUVirtualAddress());
    commandList->SetComputeRootDescriptorTable(3, {srvHeap->GetGPUDescriptorHandleForHeapStart().ptr+cbvDescriptorSize});

    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.RayGenerationShaderRecord.StartAddress = raygenTable->GetGPUVirtualAddress();
    desc.RayGenerationShaderRecord.SizeInBytes = 64;
    desc.HitGroupTable.StartAddress = hitTable->GetGPUVirtualAddress();
    desc.HitGroupTable.SizeInBytes = 64;
    desc.HitGroupTable.StrideInBytes = 64;
    desc.MissShaderTable.StartAddress = missTable->GetGPUVirtualAddress();
    desc.MissShaderTable.SizeInBytes = 64;
    desc.MissShaderTable.StrideInBytes = 64;
    desc.Width = width;
    desc.Height = height;
    desc.Depth = 1;

    commandList->SetPipelineState1(rtPSO.Get());
    commandList->DispatchRays(&desc);

    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(rtTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(_countof(preCopyBarriers), preCopyBarriers);
    commandList->CopyResource(renderTargets[frameIdx].Get(), rtTexture.Get());
    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(rtTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(_countof(postCopyBarriers), postCopyBarriers);
    commandList->Close();

    ID3D12CommandList* const commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    swapchain->Present(1, 0);

    wait_until_finished();
}

