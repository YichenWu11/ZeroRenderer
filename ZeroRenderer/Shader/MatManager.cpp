#include "matManager.h"

MatManager::MatManager() {}

MatManager::~MatManager() {}

Material* MatManager::GetMaterial(const std::string& name)
{
	return mMaterials.at(name).get();
}

void MatManager::CreateMaterial(
	const std::string& name,
	int MatCBIndex,
	int DiffuseSrvHeapIndex,
	XMFLOAT4 DiffuseAlbedo,
	XMFLOAT3 FresnelR0,
	float Roughness,
	int NormalSrvHeapIndex)
{
	auto mat = std::make_unique<Material>();
	mat->Name = name;
	mat->MatCBIndex = MatCBIndex;
	mat->DiffuseSrvHeapIndex = DiffuseSrvHeapIndex;
	mat->NormalSrvHeapIndex = NormalSrvHeapIndex;
	mat->DiffuseAlbedo = DiffuseAlbedo;
	mat->FresnelR0 = FresnelR0;
	mat->Roughness = Roughness;

	mMaterials.try_emplace(name, std::move(mat));
}

void MatManager::UpdateMaterialBuffer(UploadBuffer<MaterialData>* currMaterialBuffer)
{
	// update every material data
	for (auto& item : mMaterials)
	{
		Material* mat = item.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			mat->NumFramesDirty--;
		}
	}
}
