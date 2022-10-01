#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/UploadBuffer.h"
#include "../DXRuntime/FrameResource.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

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
