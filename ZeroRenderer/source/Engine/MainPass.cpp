#include "MainPass.h"

MainPass::MainPass(SsaoPass* pass, ShadowPass* shadow, UINT s, UINT c) :
	ssaoPass(pass), shadowPass(shadow), mSkyTexHeapIndex(s), mCbvSrvUavDescriptorSize(c) {}

void MainPass::Render(
    ComPtr<ID3D12GraphicsCommandList> mCommandList,
    FrameResource* mCurrFrameResource,
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
    ComPtr<ID3D12RootSignature> mRootSignature,
    PSOManager* psoManager,
    Scene* mScene)
{
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Rebind state whenever graphics root signature changes.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(buffer,
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	// Clear the back buffer.
	mCommandList->ClearRenderTargetView(rtvHandle, Colors::WhiteSmoke, 0, nullptr);

	// WE ALREADY WROTE THE DEPTH INFO TO THE DEPTH BUFFER IN DrawNormalsAndDepth,
	// SO DO NOT CLEAR DEPTH.

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("opaque"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Opaque), mCurrFrameResource);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("sky"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Sky), mCurrFrameResource);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("transparent"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Transparent), mCurrFrameResource);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("highlight"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Highlight), mCurrFrameResource);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(buffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));
}

void MainPass::Update(FrameResource* mCurrFrameResource, Camera& mCamera)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(view)), view);
	XMMATRIX invProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(proj)), proj);
	XMMATRIX invViewProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(viewProj)), viewProj);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);
	XMMATRIX shadowTransform = XMLoadFloat4x4(get_rvalue_ptr((shadowPass->GetShadowTransform())));

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = TotalTime;
	mMainPassCB.DeltaTime = DeltaTime;
	mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.6f, 1.0f };
	mMainPassCB.Lights[0].Direction = shadowPass->mBaseLightDirections[0];
	mMainPassCB.Lights[0].Strength = { mainLightIntensity };
	mMainPassCB.Lights[1].Direction = shadowPass->mBaseLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.1f, 0.1f, 0.1f };
	mMainPassCB.Lights[2].Direction = shadowPass->mBaseLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);

	// update ssaoPass
	ssaoPass->mMainPassCB = mMainPassCB;
	ssaoPass->mScreenViewport = mScreenViewport;
	ssaoPass->mScissorRect = mScissorRect;
}
