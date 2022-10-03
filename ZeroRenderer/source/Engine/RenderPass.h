#pragma once

#include "../Common/d3dUtil.h"

#include "../DXRuntime/FrameResource.h"

#include "../Shader/RenderItem.h"

#include "../Shader/PSOManager.h"

#include "Scene.h"

#include "../Common/Camera.h"

class RenderPass
{
public:
	RenderPass() = default;

	virtual void Render(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mRootSignature,
		PSOManager* psoManager,
        Scene* mScene) = 0;

	virtual void Update(FrameResource* mCurrFrameResource, Camera& camera) = 0;

    void DrawRenderItems(
		ID3D12GraphicsCommandList* cmdList, 
		std::vector<RenderItem*>& ritems,
		FrameResource* mCurrFrameResource)
    {
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

		auto objectCB = mCurrFrameResource->ObjectCB->Resource(); // 拿到常量缓冲区的 ID3D12Resource

		// For each render item...
		for (size_t i = 0; i < ritems.size(); ++i)
		{
			auto ri = ritems[i];

			cmdList->IASetVertexBuffers(0, 1, get_rvalue_ptr(ri->Geo->VertexBufferView()));
			cmdList->IASetIndexBuffer(get_rvalue_ptr(ri->Geo->IndexBufferView()));
			cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

			cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

			cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		}
    }
};
