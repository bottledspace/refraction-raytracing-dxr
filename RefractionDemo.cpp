#include "RefractionDemo.hpp"
#include <sstream>
#include <fstream>
#include <vector>
#include <assert.h>


void createUploadBuffer(ComPtr<ID3D12Resource>& resource, ComPtr<ID3D12Device5>& device, unsigned size)
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
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
}

void copyToBuffer(ComPtr<ID3D12Resource>& resource, void* data, unsigned size)
{
    D3D12_RANGE range = {};
    void* p;
    resource->Map(0, &range, &p);
    memcpy(p, data, size);
    resource->Unmap(0, nullptr);
}

void RefractionDemo::createDevice()
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

void RefractionDemo::createConstants()
{
    unsigned size = (sizeof(DirectX::XMFLOAT4X4) + 255u) & ~255u;
    createUploadBuffer(cameraConstantBuffer, device, size);


}


// Recreate the swapchain and associated stuff like the render targets and
// the depth/stencil buffer and command allocator+list.
void RefractionDemo::recreateSwapchain(HWND hwnd, int width, int height)
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
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, 640, 480, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
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

    // Create a synchronization object which we will use to ensure the GPU is done after swapping buffers.
    // This is temporary and a bad way of doing things. We should really just use separate command lists
    // for each back buffer so we can let the GPU continue on ahead of the CPU.
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
}

void RefractionDemo::createSignatures()
{
    // The root signature is our interface to the shaders. We are simply describing the data here,
    // we provide the contents in drawFrame each frame.

    // Main root signature
    {
    CD3DX12_ROOT_PARAMETER1 rp[4];
    rp[0].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0));
    rp[1].InitAsShaderResourceView(0);
    rp[2].InitAsConstantBufferView(0);
    rp[3].InitAsDescriptorTable(1, &CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1));
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rp), rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void RefractionDemo::createPipelineState()
{
    // Create the pipeline state, which includes compiling and loading shaders.
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

#ifdef _DEBUG
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ComPtr<ID3DBlob> errMsg;
    if (S_OK != D3DCompileFromFile(L"../Shader1.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errMsg))
        OutputDebugStringA((char*)errMsg->GetBufferPointer());
    if (S_OK != D3DCompileFromFile(L"../Shader1.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errMsg))
        OutputDebugStringA((char*)errMsg->GetBufferPointer());

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = D3D12_SHADER_BYTECODE{ vertexShader->GetBufferPointer(),vertexShader->GetBufferSize() };
    psoDesc.PS = D3D12_SHADER_BYTECODE{ pixelShader->GetBufferPointer(),pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    //psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
}

void RefractionDemo::uploadConstants()
{
    static float angle = 0.01f;
    auto proj = DirectX::XMMatrixPerspectiveFovRH(3.14f/2.0, 640.0/480.0, 0.1f, 100.0f);
    auto mv = DirectX::XMMatrixTranslation(0, 0, 10)*DirectX::XMMatrixRotationY(angle);

    const DirectX::XMMATRIX mvp(
        10, 0, 0, 0,
        0, -10, 0, 0,
        0, 0, -0.831807, -0.166528,
        0, 0, -0.831807, 0.833472);

    DirectX::XMFLOAT4X4 mvp_inv;
    DirectX::XMStoreFloat4x4(&mvp_inv, DirectX::XMMatrixInverse(nullptr, proj*mv));
    copyToBuffer(cameraConstantBuffer, &mvp_inv, sizeof(mvp_inv));
    angle += 0.01f;
}

void RefractionDemo::setupRaytracingAccelerationStructures()
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
    instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    instanceDesc.InstanceContributionToHitGroupIndex = 0;
    instanceDesc.AccelerationStructure = blasResult->GetGPUVirtualAddress();
    createUploadBuffer(instanceDescs, device, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
    copyToBuffer(instanceDescs, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

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

void RefractionDemo::setupRaytracingPipelineStateObjects()
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
    // Can be omitted, simply adds them all by default.
    /*subobjDXIL->DefineExport(L"RayGen");
    subobjDXIL->DefineExport(L"Miss");
    subobjDXIL->DefineExport(L"ClosestHit");
    subobjDXIL->DefineExport(L"AnyHit");*/

    auto subobjHit = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    subobjHit->SetHitGroupExport(L"HitGroup");
    //subobjHit->SetAnyHitShaderImport(L"AnyHit");
    subobjHit->SetClosestHitShaderImport(L"ClosestHit");
    subobjHit->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    
    auto subobjShaderConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    subobjShaderConfig->Config(sizeof(DirectX::XMFLOAT4), sizeof(DirectX::XMFLOAT2));

    auto subobjLocalSig = stateObjectDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    subobjLocalSig->SetRootSignature(localRootSignature.Get());
    auto assocSubobj = stateObjectDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    assocSubobj->SetSubobjectToAssociate(*subobjLocalSig);
    assocSubobj->AddExport(L"HitGroup");

    auto subobjRootSig = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    subobjRootSig->SetRootSignature(rootSignature.Get());
    
    auto subobjPipeline = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    subobjPipeline->Config(1);

    assert(SUCCEEDED(device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&rtPSO))));
    rtPSO->SetName(L"RayTracing PSO");
}

void RefractionDemo::createRaytracingTexture()
{
    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 640, 480, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&rtTexture));
    rtTexture->SetName(L"RayTracing Texture");
}

void RefractionDemo::createShaderTables()
{
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    rtPSO.As(&stateObjectProperties);
    void* rayGenId = stateObjectProperties->GetShaderIdentifier(L"RayGen");
    void* missId = stateObjectProperties->GetShaderIdentifier(L"Miss");
    void* hitGroupId = stateObjectProperties->GetShaderIdentifier(L"HitGroup");

    D3D12_RANGE range = { 0,0 };
    char* p;

    createUploadBuffer(raygenTable, device, 64);
    raygenTable->SetName(L"RayGen Table");
    raygenTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], rayGenId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    raygenTable->Unmap(0, nullptr);

    createUploadBuffer(hitTable, device, 64);
    hitTable->SetName(L"Hit Table");
    hitTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], hitGroupId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    hitTable->Unmap(0, nullptr);

    createUploadBuffer(missTable, device, 64);
    missTable->SetName(L"Miss Table");
    missTable->Map(0, &range, (void**)&p);
    memcpy(&p[0], missId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    missTable->Unmap(0, nullptr);
}

void RefractionDemo::waitForCommandsToFinish()
{
    InterlockedIncrement(&fenceValue);
    commandQueue->Signal(fence.Get(), fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
}

void RefractionDemo::createDescriptorHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc;
    srvDesc.NodeMask = 0;
    srvDesc.NumDescriptors = 3;
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
}

void RefractionDemo::initialize(HWND hWnd, int width, int height)
{
    createDevice();

    // We should be creating one of these per RTV, for now we just create one.
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

    createConstants();
    createSignatures();
    cubeMesh.load("../monkey.obj");
    cubeMesh.upload(device);

    
    setupRaytracingAccelerationStructures();
    setupRaytracingPipelineStateObjects();
    createRaytracingTexture();
    createShaderTables();
    recreateSwapchain(hWnd, 640, 480);
    createDescriptorHeap();

    commandList->Close();
    ID3D12CommandList* const commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    waitForCommandsToFinish();
}

void RefractionDemo::drawFrame()
{
    uploadConstants();

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
    desc.Width = 640;
    desc.Height = 480;
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

    waitForCommandsToFinish();
}

