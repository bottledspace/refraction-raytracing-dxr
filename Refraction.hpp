#pragma once

#include <Windows.h>
#include <wrl/client.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <fstream>
#include <vector>
#include "d3dx12.h"


template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

struct Vertex { float position[3]; float uv[2]; };
struct Mesh {
    std::vector<uint16_t> indices;
    std::vector<Vertex> verts;
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
};
extern Mesh cubeMesh;

struct Constants {
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4X4 view;
};

void createDevice();
void recreateSwapchain(HWND hwnd, int width, int height);
bool loadMesh(Mesh& mesh, const char* filename);
void uploadMesh(Mesh& mesh);
void uploadConstants();
void createSignature();
void createConstants();
void createPipelineState();
void drawFrame();