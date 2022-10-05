#pragma once

#include "../Common/d3dUtil.h"

using Microsoft::WRL::ComPtr;

class ShaderManager : public PublicSingleton<ShaderManager>
{
public:
	ShaderManager();
	~ShaderManager();

	std::unordered_map<std::string, ComPtr<ID3DBlob>>& GetShaders();

	std::vector<D3D12_INPUT_ELEMENT_DESC> GetInputLayout() const;

	void ControlNormalMap(bool is_normal_map);

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
};

