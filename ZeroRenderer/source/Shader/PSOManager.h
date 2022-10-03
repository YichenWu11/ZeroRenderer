#pragma once

//
// Manage and Generate automatically (may be) PSOs
//

#include "../Common/d3dUtil.h"

#include "Ssao.h"

using Microsoft::WRL::ComPtr;

class PSOManager : public PublicSingleton<PSOManager>
{
public:
	PSOManager(
		ID3D12Device* device,
		std::vector<D3D12_INPUT_ELEMENT_DESC>& mInputLayout,
		ComPtr<ID3D12RootSignature> mRootSignature,
		ComPtr<ID3D12RootSignature> mSsaoRootSignature,
		std::unordered_map<std::string, ComPtr<ID3DBlob>>& mShaders,
		DXGI_FORMAT mBackBufferFormat,
		DXGI_FORMAT mDepthStencilFormat,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

	~PSOManager();

	void CreatePipelineState(
		const std::string& psoName,
		std::unordered_map<std::string, ComPtr<ID3DBlob>>& mShaders,
		const std::string& vsName,
		const std::string& psName,
		UINT mNumRenderTargets = 1,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

	ID3D12PipelineState* GetPipelineState(const std::string&) const;

private:
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	ID3D12Device* md3dDevice;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC defaultDesc;
};
