#include "ShadowPass.h"

ShadowPass::ShadowPass(ID3D12Device* device, UINT size) : mCbvSrvUavDescriptorSize(size)
{
	mShadowMap = std::make_unique<ShadowMap>(device, 4096, 4096);
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(37.0f * 37.0f + 37.0f * 37.0f);
}

void ShadowPass::Render(
	ComPtr<ID3D12GraphicsCommandList> mCommandList,
	FrameResource* mCurrFrameResource,
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
    ComPtr<ID3D12RootSignature> mRootSignature,
	PSOManager* psoManager,
	Scene* mScene)
{
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind null SRV for shadow map pass.
	mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrv);

	//CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	//hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToShadowMap(
		mCommandList, mCurrFrameResource, mSrvDescriptorHeap,
		mNullSrv, mRootSignature, psoManager, mScene);
}

void ShadowPass::DrawSceneToShadowMap(
	ComPtr<ID3D12GraphicsCommandList> mCommandList,
	FrameResource* mCurrFrameResource,
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap,
	CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv,
	ComPtr<ID3D12RootSignature> mRootSignature,
	PSOManager* psoManager,
	Scene* mScene)
{
	mCommandList->RSSetViewports(1, get_rvalue_ptr(mShadowMap->Viewport()));
	mCommandList->RSSetScissorRects(1, get_rvalue_ptr(mShadowMap->ScissorRect()));

	// Change to DEPTH_WRITE.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE)));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Set null render target because we are only going to draw to
	// depth buffer.  Setting a null render target will disable color writes.
	mCommandList->OMSetRenderTargets(0, nullptr, false, get_rvalue_ptr(mShadowMap->Dsv()));

	// Bind the pass constant buffer for the shadow map pass.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

	// Note the active PSO also must specify a render target count of 0.
	mCommandList->SetPipelineState(psoManager->GetPipelineState("shadow_opaque"));

	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Opaque), mCurrFrameResource);
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Transparent), mCurrFrameResource);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void ShadowPass::Update(FrameResource* mCurrFrameResource, Camera& camera)
{
	UpdateShadowTransform();
	UpdateShadowPassCB(mCurrFrameResource);
}

void ShadowPass::UpdateShadowPassCB(FrameResource* mCurrFrameResource)
{
	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(view)), view);
	XMMATRIX invProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(proj)), proj);
	XMMATRIX invViewProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(viewProj)), viewProj);

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mShadowPassCB);
}

void ShadowPass::UpdateShadowTransform()
{
	//// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[0]);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}
