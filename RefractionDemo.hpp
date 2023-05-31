#pragma once

#include "stdafx.h"
#include "Mesh.hpp"

constexpr int swapchainBufferCount = 2;

struct RefractionDemo
{
    void initialize(HWND hWnd, int width, int height);
    void drawFrame();

private:
    void uploadConstants();
    void recreateSwapchain(HWND hwnd, int width, int height);
    void createDevice();
    void createConstants();
    void createSignatures();
    void createPipelineState();
    void setupRaytracingAccelerationStructures();
    void setupRaytracingPipelineStateObjects();
    void createRaytracingTexture();
    void createShaderTables();
    void waitForCommandsToFinish();

    Mesh cubeMesh;

    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D12Device5> device;

    // Cached GPU sizes for descriptors (used for offsets into descriptor tables)
    UINT rtvDescriptorSize;
    UINT dsvDescriptorSize;
    UINT cbvDescriptorSize; // also SRV and UAV

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12DescriptorHeap> cbvHeap;

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
    ComPtr<ID3D12Resource> shaderTable;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
};