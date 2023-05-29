#include <Windows.h>
#include <wrl/client.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <dxcapi.use.h>
#include "d3dx12.h"


template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;