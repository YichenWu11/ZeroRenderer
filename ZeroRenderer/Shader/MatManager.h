#pragma once

#include "../Common/d3dUtil.h"
#include "../DXRuntime/FrameResource.h"

#include "../Resource/UploadBuffer.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Material
{
	// Unique material name for lookup.
	std::string Name;

	// Index into constant buffer corresponding to this material.
	int MatCBIndex = -1;

	// Index into SRV heap for diffuse texture.
	int DiffuseSrvHeapIndex = -1;

	// Index into SRV heap for normal texture.
	int NormalSrvHeapIndex = -1;

	int NumFramesDirty = gNumFrameResources;

	// Material constant buffer data used for shading.
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

class MatManager : public PublicSingleton<MatManager>
{
public:
	MatManager();

	~MatManager();

	void CreateMaterial(
		const std::string& name,
		int MatCBIndex,
		int DiffuseSrvHeapIndex,
		XMFLOAT4 DiffuseAlbedo,
		XMFLOAT3 FresnelR0,
		float Roughness,
		int NormalSrvHeapIndex = -1);

	void UpdateMaterialBuffer(UploadBuffer<MaterialData> *);

	Material* GetMaterial(const std::string &name);

	size_t GetSize() { return mMaterials.size(); }
private:
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

};
