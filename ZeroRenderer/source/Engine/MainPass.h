#pragma once

#include "RenderPass.h"
#include "SsaoPass.h"
#include "ShadowPass.h"

#include "../../3rdparty/imgui/imgui.h"
#include "../../3rdparty/imgui/imgui_impl_dx12.h"
#include "../../3rdparty/imgui/imgui_impl_win32.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class MainPass : public RenderPass
{
public:
    MainPass(SsaoPass*, ShadowPass*, UINT, UINT);

    virtual void Render(
        ComPtr<ID3D12GraphicsCommandList> mCommandList,
        FrameResource* mCurrFrameResource,
        ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
        CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
        ComPtr<ID3D12RootSignature> mRootSignature,
        PSOManager* psoManager,
        Scene* mScene) override;

    virtual void Update(FrameResource* mCurrFrameResource, Camera& mCamera) override;

    UINT mSkyTexHeapIndex;
    UINT mCbvSrvUavDescriptorSize;
    XMFLOAT3 mainLightIntensity = { 0.9f, 0.8f, 0.7f };

    // 在 ZeroRenderer::Update 中更新 per frame
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;
    ID3D12Resource* buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
    int mClientWidth;
    int mClientHeight;
    float TotalTime;
    float DeltaTime;

private:
    SsaoPass* ssaoPass;  // 为了更新 ssaoCB 而使用的 trick
    ShadowPass* shadowPass;
    PassConstants mMainPassCB;
};