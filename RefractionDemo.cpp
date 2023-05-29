#include "RefractionDemo.hpp"
#include <sstream>
#include <fstream>
#include <vector>
#include <assert.h>

struct Camera
{
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4X4 view;
} camera;


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

    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

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
    unsigned size = (sizeof(Camera) + 255u) & ~255u;
    createUploadBuffer(cameraConstantBuffer, device, size);

    D3D12_DESCRIPTOR_HEAP_DESC cbvDesc;
    cbvDesc.NodeMask = 0;
    cbvDesc.NumDescriptors = 1;
    cbvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&cbvDesc, IID_PPV_ARGS(&cbvHeap));

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
    desc.BufferLocation = cameraConstantBuffer->GetGPUVirtualAddress();
    desc.SizeInBytes = size;
    device->CreateConstantBufferView(&desc, cbvHeap->GetCPUDescriptorHandleForHeapStart());
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

    CD3DX12_STATIC_SAMPLER_DESC statsample;
    statsample.Init(0);
    statsample.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_DESCRIPTOR_RANGE1 descRange;
    descRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    
    CD3DX12_ROOT_PARAMETER1 rp;
    rp.InitAsDescriptorTable(1, &descRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler;
    sampler.Init(0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedSig;
    D3D12SerializeVersionedRootSignature(&rootSigDesc, &serializedSig, nullptr);
    device->CreateRootSignature(0, serializedSig->GetBufferPointer(),
        serializedSig->GetBufferSize(), IID_PPV_ARGS(&rootSignature));


    // Local root signature (for raytracing)

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC localRootSigDesc;
    localRootSigDesc.Init_1_1(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    localRootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    ComPtr<ID3DBlob> serializedLocalRootSig;
    D3D12SerializeVersionedRootSignature(&localRootSigDesc, &serializedLocalRootSig, nullptr);
    device->CreateRootSignature(0, serializedLocalRootSig->GetBufferPointer(),
        serializedLocalRootSig->GetBufferSize(), IID_PPV_ARGS(&localRootSignature));
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
    XMStoreFloat4x4(&camera.proj, DirectX::XMMatrixPerspectiveFovLH(3.14f / 2.0f, 1.333f, 0.1f, 200.0f));
    XMStoreFloat4x4(&camera.view, DirectX::XMMatrixRotationY(angle) * DirectX::XMMatrixTranslation(0, 0, 10));
    copyToBuffer(cameraConstantBuffer, &camera, sizeof(Camera));
    angle += 0.01f;
}

void RefractionDemo::setupRaytracingAccelerationStructures()
{
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


    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.Transform[0][0] = 1.0f;
    instanceDesc.Transform[1][1] = 1.0f;
    instanceDesc.Transform[2][2] = 1.0f;
    instanceDesc.InstanceMask = 1;
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
}

IDxcBlob* rayGenBytecode;
dxc::DxcDllSupport dxcHelper;
IDxcCompiler* compiler;
IDxcLibrary* library;
IDxcIncludeHandler* includeHandler;

void RefractionDemo::setupRaytracingPipelineStateObjects()
{
    D3D12_STATE_SUBOBJECT stateObjects[7];

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
 
    IDxcBlobEncoding* error;
    result->GetErrorBuffer(&error);
    OutputDebugStringA((char*)error->GetBufferPointer());
    
    D3D12_EXPORT_DESC entryDesc[3] = {};
    entryDesc[0].Name = L"RayGen";
    entryDesc[1].Name = L"AnyHit";
    entryDesc[2].Name = L"ClosestHit";

    D3D12_DXIL_LIBRARY_DESC dxilDesc;
    dxilDesc.DXILLibrary.BytecodeLength = rayGenBytecode->GetBufferSize();
    dxilDesc.DXILLibrary.pShaderBytecode = rayGenBytecode->GetBufferPointer();
    dxilDesc.NumExports = _countof(entryDesc);
    dxilDesc.pExports = entryDesc;
    

    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
    hitGroupDesc.AnyHitShaderImport = L"AnyHit";
    hitGroupDesc.HitGroupExport = L"HitGroup";

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfigDesc = {};
    pipelineConfigDesc.MaxTraceRecursionDepth = 1;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfigDesc = {};
    shaderConfigDesc.MaxAttributeSizeInBytes = sizeof(float)*4;
    shaderConfigDesc.MaxPayloadSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION payloadAssociationDesc = {};
    const static wchar_t* payloadExports[] = { L"RayGen", L"HitGroup" };
    payloadAssociationDesc.NumExports = _countof(payloadExports);
    payloadAssociationDesc.pExports = payloadExports;
    payloadAssociationDesc.pSubobjectToAssociate = &stateObjects[2];

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION rootsigAssociationDesc = {};
    const static wchar_t* rootsigExports[] = { L"RayGen", L"ClosestHit", L"AnyHit", L"HitGroup" };
    rootsigAssociationDesc.NumExports = _countof(rootsigExports);
    rootsigAssociationDesc.pExports = rootsigExports;
    rootsigAssociationDesc.pSubobjectToAssociate = &stateObjects[4];
    
    D3D12_LOCAL_ROOT_SIGNATURE localRootSig = {};
    localRootSig.pLocalRootSignature = localRootSignature.Get();

    stateObjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    stateObjects[0].pDesc = &dxilDesc;
    stateObjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    stateObjects[1].pDesc = &pipelineConfigDesc;
    stateObjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    stateObjects[2].pDesc = &shaderConfigDesc;
    stateObjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    stateObjects[3].pDesc = &hitGroupDesc;
    stateObjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    stateObjects[4].pDesc = &localRootSig;
    stateObjects[5].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    stateObjects[5].pDesc = &payloadAssociationDesc;
    stateObjects[6].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    stateObjects[6].pDesc = &rootsigAssociationDesc;

    D3D12_STATE_OBJECT_DESC stateObjectDesc;
    stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects = _countof(stateObjects);
    stateObjectDesc.pSubobjects = stateObjects;
    device->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&rtPSO));
}

void RefractionDemo::waitForCommandsToFinish()
{
    InterlockedIncrement(&fenceValue);
    commandQueue->Signal(fence.Get(), fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
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
    recreateSwapchain(hWnd, 640, 480);

    createPipelineState();

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
    commandList->Reset(commandAllocator.Get(), pipelineState.Get());

    ID3D12DescriptorHeap* heaps[] = { cbvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(0, cbvHeap->GetGPUDescriptorHandleForHeapStart());

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIdx, rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = 640;
    viewport.Height = 480;
    viewport.MaxDepth = 1;
    viewport.MinDepth = 0;
    commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect;
    scissorRect.left = 0;
    scissorRect.right = scissorRect.left + viewport.Width;
    scissorRect.top = 0;
    scissorRect.bottom = scissorRect.top + viewport.Height;
    commandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.392f, 0.584f, 0.929f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cubeMesh.draw(device, commandList);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    commandList->Close();

    ID3D12CommandList* const commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    swapchain->Present(1, 0);

    waitForCommandsToFinish();
}

