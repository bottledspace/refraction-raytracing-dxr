#include "Refraction.hpp"
#include "UploadBuffer.hpp"
#include <sstream>


constexpr int swapchainBufferCount = 2;

// DirectX Graphics Infrastructure is a common interface shared by other APIs.
ComPtr<IDXGIFactory2> factory;
ComPtr<ID3D12Device5> device;
ComPtr<ID3D12CommandQueue> commandQueue;
ComPtr<ID3D12CommandAllocator> commandAllocator;
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12RootSignature> rootSignature;
ComPtr<IDXGISwapChain3> swapchain;
// Cached GPU sizes for descriptors (used for offsets into descriptor tables)
UINT rtvDescriptorSize;
UINT dsvDescriptorSize;
UINT cbvDescriptorSize; // also SRV and UAV
ComPtr<ID3D12DescriptorHeap> rtvHeap;
ComPtr<ID3D12DescriptorHeap> dsvHeap;
ComPtr<ID3D12DescriptorHeap> cbvHeap;
ComPtr<ID3D12Resource> constantBuffer;
ComPtr<ID3D12Fence> fence;
ComPtr<ID3D12Resource> depthStencilBuffer;
ComPtr<ID3D12Resource> renderTargets[swapchainBufferCount];
ComPtr<ID3D12PipelineState> pipelineState;
HANDLE fenceEvent;
volatile UINT64 fenceValue;

Mesh cubeMesh;
Constants constants;


bool loadMesh(Mesh& mesh, const char* filename)
{
    std::ifstream is(filename, std::ios_base::binary);
    if (!is.is_open())
        return false;

    std::vector<float> locs, uvs;
    std::string line;
    while (std::getline(is, line)) {
        if (float x, y, z; sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3)
            locs.insert(locs.end(), { x,y,z });
        else if (float u, v; sscanf(line.c_str(), "vt %f %f", &u, &v) == 2)
            uvs.insert(uvs.end(), { u,v });
        else if (int a[3], b[3]; sscanf(line.c_str(), "f %d/%d %d/%d %d/%d", &a[0], &b[0], &a[1], &b[1], &a[2], &b[2]) == 6) {
            float xyz[3], uv[2];
            for (int i = 0; i < 3; i++) {
                Vertex vertex;
                memcpy(vertex.position, &locs[3 * (a[i] - 1)], sizeof(float) * 3);
                memcpy(vertex.uv, &uvs[2 * (b[i] - 1)], sizeof(float) * 2);
                mesh.indices.push_back(mesh.verts.size());
                mesh.verts.push_back(vertex);
            }
        }
    }
    return true;
}

void uploadMesh(Mesh& mesh)
{
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = mesh.verts.size() * sizeof(Vertex);
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
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.vb));

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = { 0, 0 };        // We do not intend to read from this resource on the CPU.
    mesh.vb->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, mesh.verts.data(), resourceDesc.Width);
    mesh.vb->Unmap(0, nullptr);

    resourceDesc.Width = mesh.indices.size() * sizeof(uint16_t);
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.ib));

    mesh.ib->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, mesh.indices.data(), resourceDesc.Width);
    mesh.ib->Unmap(0, nullptr);
}

void drawMesh(Mesh& mesh, ComPtr<ID3D12GraphicsCommandList>& cmd)
{
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    {   D3D12_INDEX_BUFFER_VIEW view;
    view.BufferLocation = mesh.ib->GetGPUVirtualAddress();
    view.Format = DXGI_FORMAT_R16_UINT;
    view.SizeInBytes = mesh.indices.size() * sizeof(uint16_t);
    cmd->IASetIndexBuffer(&view);
    }
    {   D3D12_VERTEX_BUFFER_VIEW view;
    view.BufferLocation = mesh.vb->GetGPUVirtualAddress();
    view.StrideInBytes = sizeof(Vertex);
    view.SizeInBytes = mesh.verts.size() * sizeof(Vertex);
    cmd->IASetVertexBuffers(0, 1, &view);
    }
    cmd->DrawIndexedInstanced(mesh.indices.size(), 1, 0, 0, 0);
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

void createConstants()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvDesc;
    cbvDesc.NodeMask = 0;
    cbvDesc.NumDescriptors = 1;
    cbvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&cbvDesc, IID_PPV_ARGS(&cbvHeap));
    
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = (sizeof(Constants)+255u)&~255u;
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
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&constantBuffer));


    D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
    desc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    desc.SizeInBytes = resourceDesc.Width;
    device->CreateConstantBufferView(&desc, cbvHeap->GetCPUDescriptorHandleForHeapStart());
}


// Recreate the swapchain and associated stuff like the render targets and
// the depth/stencil buffer and command allocator+list.
void recreateSwapchain(HWND hwnd, int width, int height)
{
    // Allow re-entry, since this will be called every window resize.
    swapchain.Reset();

    // We should be creating one of these per RTV, for now we just create one.
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    // Starts in open state, close it so we can release it at the start of draw.
    commandList->Close();

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

void createSignature()
{
    // The root signature is our interface to the shaders. We are simply describing the data here,
    // we provide the contents in drawFrame each frame.

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
}

void createPipelineState()
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

static float angle = 0.01f;


void uploadConstants()
{
    // Copy the triangle data to the vertex buffer.

    XMStoreFloat4x4(&constants.proj, DirectX::XMMatrixPerspectiveFovLH(3.14f / 2.0f, 1.333f, 0.1f, 200.0f));
    XMStoreFloat4x4(&constants.view, DirectX::XMMatrixRotationY(angle) * DirectX::XMMatrixTranslation(0, 0, 10));
    angle += 0.01f;

    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = { 0, 0 };        // We do not intend to read from this resource on the CPU.
    constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);
}

void drawFrame()
{
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

    drawMesh(cubeMesh, commandList);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIdx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    commandList->Close();

    ID3D12CommandList* const commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);
    swapchain->Present(1, 0);

    InterlockedIncrement(&fenceValue);
    commandQueue->Signal(fence.Get(), fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
}

