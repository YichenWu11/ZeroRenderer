#include "SsaoPass.h"

SsaoPass::SsaoPass(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	UINT width, UINT height, D3D12_VIEWPORT screenViewport,
	D3D12_RECT scissorRect, D3D12_CPU_DESCRIPTOR_HANDLE handle) : 
	mScreenViewport(screenViewport), mScissorRect(scissorRect), dsvHeapHandle(handle)
{
    mSsao = std::make_unique<Ssao>(device, cmdList, width, height);
}

void SsaoPass::Render(
    ComPtr<ID3D12GraphicsCommandList> mCommandList,
    FrameResource* mCurrFrameResource,
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
    ComPtr<ID3D12RootSignature> mSsaoRootSignature,
    PSOManager* psoManager,
    Scene* mScene)
{
	DrawNormalsAndDepth(mCommandList, mCurrFrameResource, mSrvDescriptorHeap,
		mNullSrv, mSsaoRootSignature, psoManager, mScene);

	//
	// Compute SSAO.
	// 

	mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
	mSsao->ComputeSsao(mCommandList.Get(), mCurrFrameResource, 3);
}

void SsaoPass::DrawNormalsAndDepth(
    ComPtr<ID3D12GraphicsCommandList> mCommandList,
    FrameResource* mCurrFrameResource,
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
    ComPtr<ID3D12RootSignature> mSsaoRootSignature,
    PSOManager* psoManager,
    Scene* mScene)
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	// Change to RENDER_TARGET.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	// Clear the screen normal map and depth buffer.
	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
	mCommandList->ClearDepthStencilView(dsvHeapHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &normalMapRtv, true, &dsvHeapHandle);

	// Bind the constant buffer for this pass.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	mCommandList->SetPipelineState(psoManager->GetPipelineState("drawNormals"));

	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Opaque), mCurrFrameResource);
	//DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Transparent));

	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void SsaoPass::Update(FrameResource* mCurrFrameResource, Camera& camera)
{
	UpdateSsaoCB(mCurrFrameResource, camera);
}

void SsaoPass::UpdateSsaoCB(FrameResource* mCurrFrameResource, Camera& mCamera)
{
	SsaoConstants ssaoCB;

	XMMATRIX P = mCamera.GetProj();

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mMainPassCB.Proj;
	ssaoCB.InvProj = mMainPassCB.InvProj;
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	auto blurWeights = mSsao->CalcGaussWeights(2.5f);
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	// Coordinates given in view space.
	ssaoCB.OcclusionRadius = 0.5f;
	ssaoCB.OcclusionFadeStart = 0.2f;
	ssaoCB.OcclusionFadeEnd = 1.0f;
	ssaoCB.SurfaceEpsilon = 0.05f;

	auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
	currSsaoCB->CopyData(0, ssaoCB);
}
