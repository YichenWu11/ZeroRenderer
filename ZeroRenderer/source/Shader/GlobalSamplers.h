#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/d3dx12.h"

class GlobalSamplers : public PublicSingleton<GlobalSamplers>
{
public:
    static std::span<D3D12_STATIC_SAMPLER_DESC> GetSamplers();
    static std::span<D3D12_STATIC_SAMPLER_DESC> GetSsaoSamplers();
};
