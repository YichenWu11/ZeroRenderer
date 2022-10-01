#include "ShaderManager.h"

ShaderManager::ShaderManager()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"..\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"..\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["shadowVS"] = d3dUtil::CompileShader(L"..\\Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"..\\Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["debugVS"] = d3dUtil::CompileShader(L"..\\Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugPS"] = d3dUtil::CompileShader(L"..\\Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"..\\Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"..\\Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

ShaderManager::~ShaderManager() {}

std::unordered_map<std::string, ComPtr<ID3DBlob>>& ShaderManager::GetShaders()
{
	return mShaders;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> ShaderManager::GetInputLayout() const
{
	return mInputLayout;
}
