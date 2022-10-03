#pragma once

#include "RenderPass.h"

#include "../Shader/Ssao.h"

class SsaoPass : public RenderPass
{
public:
    SsaoPass(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT width, UINT height, D3D12_VIEWPORT screenViewport,
        D3D12_RECT scissorRect, D3D12_CPU_DESCRIPTOR_HANDLE handle);

    virtual void Render(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mSsaoRootSignature,
        PSOManager* psoManager,
        Scene* mScene) override;

    virtual void Update(FrameResource* mCurrFrameResource, Camera& camera) override;

    void UpdateSsaoCB(FrameResource* mCurrFrameResource, Camera& mCamera);

    Ssao* GetSsao() { return mSsao.get(); }

    void DrawNormalsAndDepth(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mRootSignature,
        PSOManager* psoManager,
        Scene* mScene);

    PassConstants mMainPassCB; // 用来接受 ZeroRenderer 类中的 MainPassCB

private:
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;        
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHeapHandle;

    std::unique_ptr<Ssao> mSsao;
};

