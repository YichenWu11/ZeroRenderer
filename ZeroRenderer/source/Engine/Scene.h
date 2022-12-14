#pragma once

//
// Manage all renderable object in the world
//

#include "../Common/d3dUtil.h"

#include "../Resource/UploadBuffer.h"

#include "../DXRuntime/FrameResource.h"

#include "../Shader/RenderItem.h"

class Scene : PublicSingleton<Scene>
{
public:
	Scene();
	~Scene();

	void CreateRenderItem(
		RenderLayer layer,
		XMMATRIX world,
		XMMATRIX TexTransform,
		Material* Mat,
		MeshGeometry* Geo,
		UINT IndexCount,
		UINT StartIndexLocation,
		UINT BaseVertexLocation,
		BoundingBox bounds,
		D3D12_PRIMITIVE_TOPOLOGY PrimitiveType= D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	void DeleteLastRenderItem(RenderLayer layer);

	void UpdateObjectCBs(UploadBuffer<ObjectConstants>*);

	std::vector<RenderItem*>& GetRenderLayer(RenderLayer layer) { return mRitemLayer[(int)layer]; }

	size_t GetRitemSize() const { return mAllRitems.size(); }

	size_t GetRenderLayerSize(int layer) const { return mRitemLayer[layer].size(); }

	std::vector<std::unique_ptr<RenderItem>>& GetAllRitems() { return mAllRitems; }

	std::vector<RenderItem*>& GetRenderLayer(int layer) { return mRitemLayer[layer]; }
private:
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

};
