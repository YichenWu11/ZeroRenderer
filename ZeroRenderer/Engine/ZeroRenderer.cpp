#include "ZeroRenderer.h"

const int gNumFrameResources = 3; // 三重缓冲

ZeroRenderer::ZeroRenderer(HINSTANCE hInstance) : D3DApp(hInstance) 
{
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(37.0f * 37.0f + 37.0f * 37.0f);
}

ZeroRenderer::~ZeroRenderer() {}

bool ZeroRenderer::Initialize()
{
	if (!D3DApp::Initialize()) return false;

	// Reset the command list to prepare for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// set camera position
	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	// create the shadow map
	mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(), 4096, 4096);

	matManager = std::make_unique<MatManager>();

	mScene     = std::make_unique<Scene>();

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();

	shaderManager = std::make_unique<ShaderManager>();

	// Build PSOs
	psoManager = std::make_unique<PSOManager>(
		md3dDevice.Get(), shaderManager->mInputLayout, mRootSignature,
		shaderManager->mShaders, mBackBufferFormat, mDepthStencilFormat);

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ZeroRenderer::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

//
// 每帧调用
//

void ZeroRenderer::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateShadowPassCB(gt);
}

void ZeroRenderer::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), psoManager->GetPipelineState("opaque")));

	/*shadow map*/
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind null SRV for shadow map pass.
	mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrv);

	mCommandList->SetGraphicsRootDescriptorTable(4, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToShadowMap();

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, get_rvalue_ptr(CurrentBackBufferView()), true, get_rvalue_ptr(DepthStencilView()));

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("opaque"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Opaque));

	mCommandList->SetPipelineState(psoManager->GetPipelineState("sky"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Sky));

	mCommandList->SetPipelineState(psoManager->GetPipelineState("transparent"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Transparent));

	//mCommandList->SetPipelineState(psoManager->GetPipelineState("debug"));
	//DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Debug));

	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ZeroRenderer::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ZeroRenderer::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ZeroRenderer::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
		
		// adjust the camera orientation
		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ZeroRenderer::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	// adjust the camera position
	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);

	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;

	mCamera.UpdateViewMatrix();
}

void ZeroRenderer::AnimateMaterials(const GameTimer& gt)
{

}

// 更新物体常量缓冲区
void ZeroRenderer::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();

	mScene->UpdateObjectCBs(currObjectCB);
}

void ZeroRenderer::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();

	matManager->UpdateMaterialBuffer(currMaterialBuffer);
}

// 更新渲染过程常量缓冲区(几乎每帧都要更新)
void ZeroRenderer::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(view)), view);
	XMMATRIX invProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(proj)), proj);
	XMMATRIX invViewProj = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(viewProj)), viewProj);

	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	mMainPassCB.Lights[0].Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
	mMainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	//mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	//mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	//mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	//mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ZeroRenderer::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",

		"brokenGlassDiffuseMap",
		"tileDiffuseMap",
		"defaultDiffuseMap",
		"skyCubeMap",
	};

	std::vector<std::wstring> texFilenames =
	{
		L"..\\Asset\\bricks2.dds",
		L"..\\Asset\\bricks2_nmap.dds",

		L"..\\Asset\\BrokenGlass.dds",
		L"..\\Asset\\tile.dds",
		L"..\\Asset\\white1x1.dds",
		L"..\\Asset\\snowcube1024.dds",
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}

void ZeroRenderer::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 2, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsConstantBufferView(0);    // 绑 objCB
	slotRootParameter[1].InitAsConstantBufferView(1);    // 绑 passCB
	slotRootParameter[2].InitAsShaderResourceView(0, 1); // 绑 matBuffer
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL); // 绑 cubemap / shadow
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL); // 绑 diffusemap(array)

	auto staticSamplers = GlobalSamplers::GetSamplers(); // std::span

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ZeroRenderer::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	// Add +1 DSV for shadow map.
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2; // one for screen and one for shadow map
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void ZeroRenderer::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 10;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // shader_visible
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList = {
		mTextures["bricksDiffuseMap"]->Resource,
		mTextures["bricksNormalMap"]->Resource,
		mTextures["tileDiffuseMap"]->Resource,
		mTextures["defaultDiffuseMap"]->Resource,
		mTextures["brokenGlassDiffuseMap"]->Resource,
	};

	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	
	for (UINT i = 0; i < (UINT)tex2DList.size(); ++i)
	{
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

	mSkyTexHeapIndex = (UINT)tex2DList.size();
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);    // index : 4

	mShadowMapHeapIndex = mSkyTexHeapIndex + 1;

	mNullCubeSrvIndex = mShadowMapHeapIndex + 1;
	mNullTexSrvIndex = mNullCubeSrvIndex + 1;

	auto srvCpuStart = mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();


	auto nullSrv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);
	mNullSrv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mNullCubeSrvIndex, mCbvSrvUavDescriptorSize);

	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 1, mDsvDescriptorSize));
}

void ZeroRenderer::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ZeroRenderer::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			2, (UINT)mScene->GetRitemSize(), (UINT)matManager->GetSize()));
	}
}

void ZeroRenderer::BuildMaterials()
{
	/*
		Args List:
			materialName         : material 在 map of manager 中的 key
			MatCBIndex           : 在 materialData 结构化缓冲区中的索引
			DiffuseSrvHeapIndex  : material 的 texture 在 srvHeap 中的索引
			DiffuseAlbedo        : 反照率
			FresnelR0            : 菲涅尔系数
			Roughness            : 粗糙度
			NormalSrvHeapIndex   : material 的 normal_map 在 srvHeap 中的索引 (none --> -1)
	*/

	matManager->CreateMaterial("bricks0", 
		0, 0, 
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), 
		XMFLOAT3(0.1f, 0.1f, 0.1f), 0.3f, 1);  // with normal_map

	matManager->CreateMaterial("tile0", 
		1, 2, 
		XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), 
		XMFLOAT3(0.2f, 0.2f, 0.2f), 0.1f);

	matManager->CreateMaterial("mirror0",
		2, 3,
		XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f),
		XMFLOAT3(0.98f, 0.97f, 0.95f), 0.1f);

	matManager->CreateMaterial("brokenGlass0",
		3, 4,
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.727811f, 0.626959f, 0.626959f), 1.0f - 0.088f);

	matManager->CreateMaterial("sky",
		5, 5,
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.1f, 0.1f, 0.1f), 1.0f);
}

void ZeroRenderer::BuildRenderItems()
{
	auto general_geo = mGeometries["shapeGeo"].get();

	/*
		Args List:
			layer              : 该 Ritem 所属的 RenderLayer
			world              : 世界矩阵
			TexTransform       : 纹理变换矩阵
			ObjCBIndex         : 其在物体常量缓冲区中的索引
			Mat                : 该 Ritem 的材质
			Geo                : 该物体所属的 Mesh（其中包含顶点索引缓冲区和拓扑类型等，用于绘制时绑定）
			IndexCount         : Number of indices read from the index buffer for each instance
			StartIndexLocation : 在索引缓冲区中的 offset
			BaseVertexLocation : 在顶点缓冲区中的 offset
			PrimitiveType      : 该图元的拓扑类型 (默认为 TRIANGLELIST)
	*/

	mScene->CreateRenderItem(
		RenderLayer::Sky,
		XMMatrixScaling(5000.0f, 5000.0f, 5000.0f),
		XMMatrixIdentity(),
		0,
		matManager->GetMaterial("sky"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Opaque,
		XMMatrixScaling(3.0f, 3.0f, 3.0f),
		XMMatrixScaling(8.0f, 8.0f, 1.0f),
		1,
		matManager->GetMaterial("tile0"),
		general_geo,
		general_geo->DrawArgs["grid"].IndexCount,
		general_geo->DrawArgs["grid"].StartIndexLocation,
		general_geo->DrawArgs["grid"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Opaque,
		XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f),
		XMMatrixIdentity(),
		2,
		matManager->GetMaterial("mirror0"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Opaque,
		XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(8.0f, 6.0f, 3.0f),
		XMMatrixIdentity(),
		3,
		matManager->GetMaterial("bricks0"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Transparent,
		XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-8.0f, 6.0f, -3.0f),
		XMMatrixIdentity(),
		4,
		matManager->GetMaterial("brokenGlass0"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Debug,
		XMMatrixRotationX(45.0f),
		XMMatrixIdentity(),
		5,
		matManager->GetMaterial("bricks0"),
		general_geo,
		general_geo->DrawArgs["quad"].IndexCount,
		general_geo->DrawArgs["quad"].StartIndexLocation,
		general_geo->DrawArgs["quad"].BaseVertexLocation
	);

	mScene->CreateRenderItem(
		RenderLayer::Opaque,
		XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(16.0f, 6.0f, 6.0f),
		XMMatrixIdentity(),
		6,
		matManager->GetMaterial("tile0"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation
	);
}

void ZeroRenderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
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

void ZeroRenderer::DrawSceneToShadowMap()
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
	// Note the active PSO also must specify a render target count of 0.
	mCommandList->OMSetRenderTargets(0, nullptr, false, get_rvalue_ptr(mShadowMap->Dsv()));

	// Bind the pass constant buffer for the shadow map pass.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

	mCommandList->SetPipelineState(psoManager->GetPipelineState("shadow_opaque"));

	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Opaque));

	mCommandList->SetPipelineState(psoManager->GetPipelineState("transparent"));
	DrawRenderItems(mCommandList.Get(), mScene->GetRenderLayer(RenderLayer::Transparent));

	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ)));
}

void ZeroRenderer::UpdateShadowTransform(const GameTimer& gt)
{
	//// Only the first "main" light casts a shadow.
	XMFLOAT3 dir(0.57735f, -0.57735f, 0.57735f);
	XMVECTOR lightDir = XMLoadFloat3(&dir);
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
	//XMMATRIX lightProj = XMMatrixPerspectiveLH(fabs(l - r), fabs(b - t), n, f);

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

void ZeroRenderer::UpdateShadowPassCB(const GameTimer& gt)
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
