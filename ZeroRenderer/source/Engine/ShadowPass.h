#pragma once

#include "RenderPass.h"

#include "../Shader/ShadowMap.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class ShadowPass : public RenderPass
{
public:
	ShadowPass(ID3D12Device* device);

	virtual void Render(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mRootSignature,
        PSOManager* psoManager,
        Scene* mScene) override;

	virtual void Update(FrameResource* mCurrFrameResource, Camera& camera) override;

    void DrawSceneToShadowMap(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mRootSignature,
        PSOManager* psoManager,
        Scene* mScene
    );

    void UpdateShadowPassCB(FrameResource* mCurrFrameResource);

    void UpdateShadowTransform();

    ShadowMap* GetShadowMap() { return mShadowMap.get(); }

    XMFLOAT4X4 GetShadowTransform() { return mShadowTransform; }

private:
	std::unique_ptr<ShadowMap> mShadowMap;

    DirectX::BoundingSphere mSceneBounds;

public:
    float mLightNearZ = 0.0f;
    float mLightFarZ = 0.0f;
    XMFLOAT3 mLightPosW;
    XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
    XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    XMFLOAT3 mBaseLightDirections[3] = {
        XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(0.0f, -0.707f, -0.707f)
    };

    PassConstants mShadowPassCB;
};
