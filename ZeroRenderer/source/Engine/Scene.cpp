#include "Scene.h"

Scene::Scene() {}

Scene::~Scene() {}

void Scene::CreateRenderItem(
	RenderLayer layer,
	XMMATRIX world,
	XMMATRIX TexTransform,
	Material* Mat,
	MeshGeometry* Geo,
	UINT IndexCount,
	UINT StartIndexLocation,
	UINT BaseVertexLocation,
	BoundingBox bounds,
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType)
{
	auto item = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&item->World,        world);
	XMStoreFloat4x4(&item->TexTransform, TexTransform);
	item->ObjCBIndex = (UINT)GetRitemSize();
	item->Mat = Mat;
	item->Geo = Geo;
	item->IndexCount = IndexCount;
	item->StartIndexLocation = StartIndexLocation;
	item->BaseVertexLocation = BaseVertexLocation;
	item->Bounds = bounds;
	item->PrimitiveType = PrimitiveType;

	mRitemLayer[(int)layer].push_back(item.get());
	mAllRitems.push_back(std::move(item));
}

void Scene::DeleteLastRenderItem(RenderLayer layer)
{
	if (!mRitemLayer[(int)layer].empty())
	{
		mRitemLayer[(int)layer].pop_back();
		mAllRitems.pop_back();
	}
}

void Scene::UpdateObjectCBs(UploadBuffer<ObjectConstants>* currObjectCB)
{
	for (auto& item : mAllRitems)
	{
		if (item->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&item->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&item->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = item->Mat->MatCBIndex;

			currObjectCB->CopyData(item->ObjCBIndex, objConstants);

			item->NumFramesDirty--;
		}
	}
}
