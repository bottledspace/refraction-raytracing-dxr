#pragma once

#include "stdafx.h"

struct RaytracingPass
{
    void initialize();

private:
    ComPtr<ID3D12Resource> blasScratch;
    ComPtr<ID3D12Resource> blasResult;
};