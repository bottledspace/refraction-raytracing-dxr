#pragma once

#include "stdafx.h"

struct Vertex
{
    float position[3];
    float uv[2];
};

struct Mesh
{
    bool load(const char* filename);
    D3D12_RAYTRACING_GEOMETRY_DESC raytracingGeometry() const;
    void upload(ComPtr<ID3D12Device5>& device);
    void draw(ComPtr<ID3D12Device5>& device, ComPtr<ID3D12GraphicsCommandList5>& cmd);

private:
    std::vector<uint16_t> indices;
    std::vector<Vertex> verts;
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
};