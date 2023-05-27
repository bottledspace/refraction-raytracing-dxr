#pragma once

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_3.h>
#include "d3dx12.h"

template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

struct UploadBuffer
{
	UploadBuffer(ComPtr<ID3D12Device>& device, UINT64 size);

	void* map();
	void unmap();



private:
    ComPtr<ID3D12Resource> _buffer;
    UINT _size;
};

UploadBuffer::UploadBuffer(ComPtr<ID3D12Device>& device, UINT64 size)
    : _size(size)
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
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_buffer)); 
}

void* UploadBuffer::map()
{
    void* data;
    D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this resource on the CPU.
    _buffer->Map(0, &readRange, reinterpret_cast<void**>(&data));
    return data;
}

void UploadBuffer::unmap()
{
    _buffer->Unmap(0, nullptr);
}