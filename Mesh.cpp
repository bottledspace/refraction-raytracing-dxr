#include "Mesh.hpp"

#include <fstream>
#include <vector>

bool Mesh::load(const char* filename)
{
    std::ifstream is(filename, std::ios_base::binary);
    if (!is.is_open())
        return false;

    std::vector<float> locs, uvs, norms;
    std::string line;
    while (std::getline(is, line)) {
        if (float x, y, z; sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3)
            locs.insert(locs.end(), { x,y,z });
        else if (float u, v; sscanf(line.c_str(), "vt %f %f", &u, &v) == 2)
            uvs.insert(uvs.end(), { u,v });
        else if (float x, y, z; sscanf(line.c_str(), "vn %f %f %f", &x, &y, &z) == 3)
            norms.insert(norms.end(), { x,y,z });
        else if (int a[3], b[3], c[3];
                sscanf(line.c_str(), "f %d/%d/%d %d/%d/%d %d/%d/%d",
                    &a[0], &b[0], &c[0],
                    &a[1], &b[1], &c[1],
                    &a[2], &b[2], &c[2]) == 9) {
            for (int i = 0; i < 3; i++) {
                Vertex vertex = {};
                memcpy(vertex.position, &locs[3 * (a[i] - 1)], sizeof(float) * 3);
                memcpy(vertex.uv, &uvs[2 * (b[i] - 1)], sizeof(float) * 2);
                memcpy(vertex.norm, &norms[3 * (c[i] - 1)], sizeof(float) * 3);
                indices.push_back(verts.size());
                verts.push_back(vertex);
            }
        }
    }
    return true;
}

D3D12_RAYTRACING_GEOMETRY_DESC Mesh::raytracingGeometry() const
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc;
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
    geometryDesc.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geometryDesc.Triangles.VertexCount = verts.size();
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.IndexBuffer = ib->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometryDesc.Triangles.IndexCount = indices.size();
    geometryDesc.Triangles.Transform3x4 = 0;
    return geometryDesc;
}

void Mesh::upload(ComPtr<ID3D12Device5>& device)
{
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = verts.size() * sizeof(Vertex);
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
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vb));

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = { 0, 0 };        // We do not intend to read from this resource on the CPU.
    vb->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, verts.data(), resourceDesc.Width);
    vb->Unmap(0, nullptr);

    resourceDesc.Width = indices.size() * sizeof(uint32_t);
    device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ib));

    ib->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, indices.data(), resourceDesc.Width);
    ib->Unmap(0, nullptr);
}

void Mesh::draw(ComPtr<ID3D12Device5>& device, ComPtr<ID3D12GraphicsCommandList5>& cmd)
{
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_INDEX_BUFFER_VIEW indexView;
    indexView.BufferLocation = ib->GetGPUVirtualAddress();
    indexView.Format = DXGI_FORMAT_R32_UINT;
    indexView.SizeInBytes = indices.size() * sizeof(uint32_t);
    cmd->IASetIndexBuffer(&indexView);

    D3D12_VERTEX_BUFFER_VIEW vertexView;
    vertexView.BufferLocation = vb->GetGPUVirtualAddress();
    vertexView.StrideInBytes = sizeof(Vertex);
    vertexView.SizeInBytes = verts.size() * sizeof(Vertex);
    cmd->IASetVertexBuffers(0, 1, &vertexView);

    cmd->DrawIndexedInstanced(indices.size(), 1, 0, 0, 0);
}