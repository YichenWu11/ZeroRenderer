#include "ZeroRenderer.h"
#include <windowsx.h>
#include "ResourceUploadBatch.h"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"

const int gNumFrameResources = 3;

const int maxObjectNum = 100;

const int mouseMoveSensity = 1;

ZeroRenderer::ZeroRenderer(HINSTANCE hInstance) : D3DApp(hInstance) 
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
}

ZeroRenderer::~ZeroRenderer() 
{
	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool ZeroRenderer::Initialize()
{
	if (!D3DApp::Initialize()) return false;

	// Reset the command list to prepare for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// set camera position
	mCamera.SetPosition(0.0f, 4.0f, -15.0f);

	matManager = std::make_unique<MatManager>();

	mScene     = std::make_unique<Scene>();

	// RenderPass
	shadowPass = std::make_unique<ShadowPass>(md3dDevice.Get(), mCbvSrvUavDescriptorSize);

	ssaoPass = std::make_unique<SsaoPass>(md3dDevice.Get(), mCommandList.Get(),
		mClientWidth, mClientHeight, mScreenViewport, mScissorRect, DepthStencilView());

	LoadTextures();
	BuildRootSignature();
	BuildSsaoRootSignature();
	BuildDescriptorHeaps();
	BuildShapeGeometry();
	BuildModelGeometry("asset\\models\\Pikachu.txt", "pikachu", "pikaGeo", false, false);
	BuildModelGeometry("asset\\models\\Squirtle.txt", "squirtle", "squiGeo", false, false);
	BuildModelGeometry("asset\\models\\marry.txt", "marry", "marryGeo", true, true);
	BuildModelGeometry("asset\\models\\cow.txt", "cow", "cowGeo", true, true);
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(mhMainWnd);
	ImGui_ImplDX12_Init(md3dDevice.Get(), gNumFrameResources,
		DXGI_FORMAT_R8G8B8A8_UNORM, mSrvDescriptorHeap.Get(),
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	mainPass = std::make_unique<MainPass>(ssaoPass.get(), shadowPass.get(), mSkyTexHeapIndex, mCbvSrvUavDescriptorSize);

	shaderManager = std::make_unique<ShaderManager>();

	// Build PSOs
	psoManager = std::make_unique<PSOManager>(
		md3dDevice.Get(), shaderManager->mInputLayout, mRootSignature, mSsaoRootSignature,
		shaderManager->mShaders, mBackBufferFormat, mDepthStencilFormat);

	ssaoPass->GetSsao()->SetPSOs(psoManager->GetPipelineState("ssao"), psoManager->GetPipelineState("ssaoBlur"));

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

	if (ssaoPass != nullptr && ssaoPass->GetSsao() != nullptr)
	{
		ssaoPass->GetSsao()->OnResize(mClientWidth, mClientHeight);

		ssaoPass->GetSsao()->RebuildDescriptors(mDepthStencilBuffer.Get());
	}
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

	mainPass->buffer = CurrentBackBuffer();
	mainPass->rtvHandle = CurrentBackBufferView();
	mainPass->dsvHandle = DepthStencilView();
	mainPass->mScreenViewport = mScreenViewport;
	mainPass->mScissorRect = mScissorRect;
	mainPass->mClientWidth = mClientWidth;
	mainPass->mClientHeight = mClientHeight;
	mainPass->TotalTime = gt.TotalTime();
	mainPass->DeltaTime = gt.DeltaTime();

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	mainPass->Update(mCurrFrameResource, mCamera);
	shadowPass->Update(mCurrFrameResource, mCamera);
	ssaoPass->Update(mCurrFrameResource, mCamera);
}

void ZeroRenderer::DrawImGui()
{
	static bool show_demo_window = false;
	static bool show_style = false;
	static bool add_render_item = true;

	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowBgAlpha(1.0f);

	if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);

	{
		ImGui::Begin("Scene And Objects");

		ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);

		ImGui::Checkbox("Demo Window", &show_demo_window);
		ImGui::Checkbox("Style Editor", &show_style);
		ImGui::Checkbox("Add RenderItem", &add_render_item);

		ImGui::Text("\nChange the MainLight Intensity / Color");
		ImGui::ColorEdit3("Color", (float*)&(mainPass->mainLightIntensity));

		ImGui::Text("\nApplication average %.3f ms/frame (%.1f FPS)\n", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

		if (show_style) ImGui::ShowStyleEditor();

		static float pos_x = 0.0f;
		static float pos_y = 0.0f;
		static float pos_z = 0.0f;

		static XMFLOAT3 obj_rotate_axis = { 0.0f, 1.0f, 0.0f };
		static float obj_rotate_angle = 0.0f;

		static XMFLOAT3 last_obj_rotate_axis = { 0.0f, 1.0f, 0.0f };
		static float last_obj_rotate_angle = 0.0f;

		if (mPickedRitem)
		{
			ImGui::Text("\nChange Position and Orientation of the Picked\n");

			XMMATRIX world_matrix = XMLoadFloat4x4(&mPickedRitem->World);
			pos_x = mPickedRitem->World._41;
			pos_y = mPickedRitem->World._42;
			pos_z = mPickedRitem->World._43;

			// rotation
			ImGui::InputFloat3("rotate axis", (float*)&(obj_rotate_axis));
			//ImGui::SliderFloat("rotate angle", &obj_rotate_angle, 0.0f, 180.0f);
			ImGui::InputFloat("rotate angle", &obj_rotate_angle, 0.0f, 180.0f);

			if (!XMQuaternionEqual(
				Quaternion(
					XMLoadFloat3(&obj_rotate_axis),
					XMConvertToRadians(obj_rotate_angle)),
				Quaternion(
					XMLoadFloat3(&last_obj_rotate_axis),
					XMConvertToRadians(last_obj_rotate_angle))))
			{
				XMStoreFloat4x4(
					&(mPickedRitem->World),
					XMMatrixRotationQuaternion(
						Quaternion(
							XMLoadFloat3(&obj_rotate_axis),
							XMConvertToRadians(obj_rotate_angle))) *
					world_matrix
				);
			}
			
			// translation
			ImGui::SliderFloat("X_Pos", &pos_x, -50.f, 50.f);
			ImGui::SliderFloat("Y_Pos", &pos_y, -50.f, 50.f);
			ImGui::SliderFloat("Z_Pos", &pos_z, -50.f, 50.f);
			mPickedRitem->World._41 = pos_x;
			mPickedRitem->World._42 = pos_y;
			mPickedRitem->World._43 = pos_z;

			mPickedRitem->NumFramesDirty = gNumFrameResources;

			last_obj_rotate_axis = obj_rotate_axis;
			last_obj_rotate_angle = obj_rotate_angle;
		}

		ImGui::End();
	}

	ImGui::SetNextWindowBgAlpha(1.0f);

	if (add_render_item)
	{

		static XMFLOAT3 world_pos = { 5.0f, 0.0f, -20.0f };
		static XMFLOAT3 world_scale = { 4.0f, 4.0f, 4.0f };
		static XMFLOAT3 rotate_axis = { 0.0f, 1.0f, 0.0f };
		static float rotate_angle = 180.0f;

		static XMFLOAT3 tex_transform = { 1.0f, 1.0f, 1.0f };

		ImGui::Begin("Edit RenderItem", &add_render_item);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)

		ImGui::SetWindowPos(ImVec2(0, 720), ImGuiCond_Always);

		ImGui::Text("Add RenderItem to Scene\n");

		static const char* layers[] = { "Opaque", "Sky", "Transparent", "Debug" };
		static int layer = 0;
		ImGui::Combo("Layer", &layer, layers, IM_ARRAYSIZE(layers));

		ImGui::InputFloat3("world pos matrix", (float*)&(world_pos));
		ImGui::InputFloat3("world scale matrix", (float*)&(world_scale));

		ImGui::InputFloat3("rotate axis", (float*)&(rotate_axis));
		ImGui::InputFloat("rotate angle", &rotate_angle, 0.0f, 360.0f);

		ImGui::InputFloat3("texture scale matrix", (float*)&(tex_transform));

		static const char* material_items[] = { "bricks0", "tile0", "mirror0", "brokenGlass0", "sky", "marry"};
		static int material_item = 2;
		ImGui::Combo("Material", &material_item, material_items, IM_ARRAYSIZE(material_items));

		static const char* shape_items[] = { "box", "grid", "sphere", "cylinder", "marry", "squirtle", "pikachu", "cow"};
		static int shape_item = 4;
		ImGui::Combo("Shape", &shape_item, shape_items, IM_ARRAYSIZE(shape_items));

		if (ImGui::Button("CreateItem"))
		{
			// 限制场景中物体数量
			if (mScene->GetRitemSize() + 1 <= maxObjectNum)
			{
				MeshGeometry* general_geo;
				if (shape_item < 4)
					general_geo = mGeometries["shapeGeo"].get();
				else if (shape_item == 4)
					general_geo = mGeometries["marryGeo"].get();
				else if (shape_item == 5)
					general_geo = mGeometries["squiGeo"].get();
				else if(shape_item == 6)
					general_geo = mGeometries["pikaGeo"].get();
				else
					general_geo = mGeometries["cowGeo"].get();

				mScene->CreateRenderItem(
					static_cast<RenderLayer>(layer),
					/* first scale, rotate, then translation */
					XMMatrixScaling(world_scale.x, world_scale.y, world_scale.z) * 
					XMMatrixRotationQuaternion(Quaternion(XMLoadFloat3(&rotate_axis), XMConvertToRadians(rotate_angle))) *
					XMMatrixTranslation(world_pos.x, world_pos.y, world_pos.z),
					XMMatrixScaling(tex_transform.x, tex_transform.y, tex_transform.z),
					matManager->GetMaterial(material_items[material_item]),
					general_geo,
					general_geo->DrawArgs[shape_items[shape_item]].IndexCount,
					general_geo->DrawArgs[shape_items[shape_item]].StartIndexLocation,
					general_geo->DrawArgs[shape_items[shape_item]].BaseVertexLocation,
					general_geo->DrawArgs[shape_items[shape_item]].Bounds
				);
			}
		}

		if (ImGui::Button("DeleteLastItem"))
		{
			if ((mScene->GetRenderLayer(RenderLayer(layer))[mScene->GetRenderLayerSize(layer)-1] == mPickedRitem))
			{
				mPickedRitem = nullptr;
			}
			mScene->DeleteLastRenderItem(RenderLayer(layer));
		}

		ImGui::End();
	}

	ImGui::Render();
}

void ZeroRenderer::PopulateCommandList(const GameTimer& gt)
{
	auto cmdListHandle = mCurrFrameResource->Command();

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	//************************ Render Pass *******************************

	shadowPass->Render(
		mCommandList, mCurrFrameResource, mSrvDescriptorHeap,
		mNullSrv, mRootSignature, psoManager.get(), mScene.get());

	ssaoPass->Render(
		mCommandList, mCurrFrameResource, mSrvDescriptorHeap,
		mNullSrv, mSsaoRootSignature, psoManager.get(), mScene.get());

	mainPass->Render(
		mCommandList, mCurrFrameResource, mSrvDescriptorHeap,
		mNullSrv, mRootSignature, psoManager.get(), mScene.get());
}

// Sync
void ZeroRenderer::SubmitCommandList(const GameTimer& gt)
{
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ZeroRenderer::Draw(const GameTimer& gt)
{
	DrawImGui();
	PopulateCommandList(gt);
	SubmitCommandList(gt);
}

void ZeroRenderer::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	if ((btnState & MK_LBUTTON) != 0) Pick(x, y);
	

	SetCapture(mhMainWnd);
}

void ZeroRenderer::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ZeroRenderer::OnMouseMove(WPARAM btnState, int x, int y)
{
	// && enable_camera_move
	if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
		
		// adjust the camera orientation
		mCamera.Pitch(dy / mouseMoveSensity);
		mCamera.RotateY(dx / mouseMoveSensity);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ZeroRenderer::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
	
	if (enable_camera_move)
	{
		// adjust the camera position
		if (GetAsyncKeyState('W') & 0x8000)
			mCamera.Walk(10.0f * dt);

		if (GetAsyncKeyState('S') & 0x8000)
			mCamera.Walk(-10.0f * dt);

		if (GetAsyncKeyState('A') & 0x8000)
			mCamera.Strafe(-10.0f * dt);

		if (GetAsyncKeyState('D') & 0x8000)

			mCamera.Strafe(10.0f * dt);

		if (GetAsyncKeyState('I') & 0x8000)
			shadowPass->mBaseLightDirections[0].z += 1.0f * dt;

		if (GetAsyncKeyState('K') & 0x8000)
			shadowPass->mBaseLightDirections[0].z -= 1.0f * dt;

		if (GetAsyncKeyState('J') & 0x8000)
			shadowPass->mBaseLightDirections[0].x += 1.0f * dt;

		if (GetAsyncKeyState('L') & 0x8000)
			shadowPass->mBaseLightDirections[0].x -= 1.0f * dt;

		if (GetAsyncKeyState('U') & 0x8000)
			shadowPass->mBaseLightDirections[0].y += 1.0f * dt;

		if (GetAsyncKeyState('O') & 0x8000)
			shadowPass->mBaseLightDirections[0].y -= 1.0f * dt;

		if (GetAsyncKeyState('R') & 0x8000)
			if (mPickedRitem)
			{
				mPickedRitem->Visible = true;
				mPickedRitem = nullptr;
			}

		mCamera.UpdateViewMatrix();
	}
}

void ZeroRenderer::AnimateMaterials(const GameTimer& gt)
{

}

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

void ZeroRenderer::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",

		"brokenGlassDiffuseMap",

		"tileDiffuseMap",
		"tileNormalMap",

		"defaultDiffuseMap",
		"marryDiffuseMap",
		"skyCubeMap",
	};

	std::vector<std::wstring> texFilenames =
	{
		L"asset\\texture\\common\\bricks2.dds",
		L"asset\\texture\\common\\bricks2_nmap.dds",
		
		L"asset\\texture\\common\\BrokenGlass.dds",
				
		L"asset\\texture\\common\\tile.dds",
		L"asset\\texture\\common\\tile_nmap.dds",
			
		L"asset\\texture\\common\\white1x1.dds",
		L"asset\\texture\\marry\\marry.dds",
		L"asset\\texture\\sky\\snowcube1024.dds",
	};

	DirectX::ResourceUploadBatch resourceUpload(md3dDevice.Get());

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		resourceUpload.Begin();
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];

		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(
			md3dDevice.Get(),
			resourceUpload,
			texFilenames[i].c_str(),
			texMap->Resource.ReleaseAndGetAddressOf()
		));

		mTextures[texMap->Name] = std::move(texMap);
		auto uploadResourcesFinished = resourceUpload.End(
			mCommandQueue.Get());

		uploadResourcesFinished.wait();
	}
}

void ZeroRenderer::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 3, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GlobalSamplers::GetSamplers();;

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

void ZeroRenderer::BuildSsaoRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstants(1, 1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GlobalSamplers::GetSsaoSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
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
		IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

void ZeroRenderer::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +6 RTV for cube render target.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
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
	srvHeapDesc.NumDescriptors = 20;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // shader_visible
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList = {
		mTextures["bricksDiffuseMap"]->Resource,  // for imgui font
		mTextures["bricksDiffuseMap"]->Resource,
		mTextures["bricksNormalMap"]->Resource,
		mTextures["tileDiffuseMap"]->Resource,
		mTextures["tileNormalMap"]->Resource,
		mTextures["defaultDiffuseMap"]->Resource,
		mTextures["brokenGlassDiffuseMap"]->Resource,
		mTextures["marryDiffuseMap"]->Resource
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

	//srvDesc.Format = tex2DList[(UINT)tex2DList.size() - 1]->GetDesc().Format;
	//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	//srvDesc.Texture3D.MipLevels = tex2DList[(UINT)tex2DList.size() - 1]->GetDesc().MipLevels;
	//srvDesc.Texture3D.MostDetailedMip = 0;
	//srvDesc.Texture3D.ResourceMinLODClamp = 0.f;
	//md3dDevice->CreateShaderResourceView(tex2DList[(UINT)tex2DList.size() - 1].Get(), &srvDesc, hDescriptor);
	//hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

	mSkyTexHeapIndex = (UINT)tex2DList.size();
	mShadowMapHeapIndex = mSkyTexHeapIndex + 1;
	mSsaoHeapIndexStart = mShadowMapHeapIndex + 1;
	mSsaoAmbientMapIndex = mSsaoHeapIndexStart + 3;
	mNullCubeSrvIndex = mSsaoHeapIndexStart + 5;
	mNullTexSrvIndex1 = mNullCubeSrvIndex + 1;
	mNullTexSrvIndex2 = mNullTexSrvIndex1 + 1;

	auto nullSrv = GetCpuSrv(mNullCubeSrvIndex);
	mNullSrv = GetGpuSrv(mNullCubeSrvIndex);

	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
	md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	shadowPass->GetShadowMap()->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	ssaoPass->GetSsao()->BuildDescriptors(
		mDepthStencilBuffer.Get(),
		GetCpuSrv(mSsaoHeapIndexStart),
		GetGpuSrv(mSsaoHeapIndexStart),
		GetRtv(SwapChainBufferCount),
		mCbvSrvUavDescriptorSize,
		mRtvDescriptorSize);
}

void ZeroRenderer::BuildModelGeometry(const char* path, const char* modelname, const char* geoname, bool is_normal, bool is_uv)
{
	std::ifstream fin(path);

	if (!fin)
	{
		MessageBox(0, L"file not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		if (is_normal) fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
		else vertices[i].Normal = vertices[i].Pos;

		if (is_uv) 
		{ 
			fin >> vertices[i].TexC.x >> vertices[i].TexC.y; 
		}
		else vertices[i].TexC = { vertices[i].Pos.x, vertices[i].Pos.y };

		XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

		XMVECTOR N = XMLoadFloat3(&vertices[i].Normal);

		XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		if (fabsf(XMVectorGetX(XMVector3Dot(N, up))) < 1.0f - 0.001f)
		{
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(up, N));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}
		else
		{
			up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
			XMVECTOR T = XMVector3Normalize(XMVector3Cross(N, up));
			XMStoreFloat3(&vertices[i].TangentU, T);
		}

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = geoname;

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
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	submesh.Bounds = bounds;

	geo->DrawArgs[modelname] = submesh;

	mGeometries[geo->Name] = std::move(geo);
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

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;

		XMVECTOR P = XMLoadFloat3(&box.Vertices[i].Position);

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	// FIX: BoundingBox Error
	BoundingBox box_bounds;
	XMStoreFloat3(&box_bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&box_bounds.Extents, 0.5f * (vMax - vMin));
	boxSubmesh.Bounds = box_bounds;

	vMin = XMLoadFloat3(&vMinf3);
	vMax = XMLoadFloat3(&vMaxf3);
	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;

		XMVECTOR P = XMLoadFloat3(&grid.Vertices[i].Position);

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox grid_bounds;
	XMStoreFloat3(&grid_bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&grid_bounds.Extents, XMVectorSet(0.0f,0.0f,0.0f, 0.0f));
	//XMStoreFloat3(&grid_bounds.Extents, 0.5f * (vMax - vMin));
	gridSubmesh.Bounds = grid_bounds;

	vMin = XMLoadFloat3(&vMinf3);
	vMax = XMLoadFloat3(&vMaxf3);
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;

		XMVECTOR P = XMLoadFloat3(&sphere.Vertices[i].Position);

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox sphere_bounds;
	XMStoreFloat3(&sphere_bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&sphere_bounds.Extents, 0.5f * (vMax - vMin));
	sphereSubmesh.Bounds = sphere_bounds;

	vMin = XMLoadFloat3(&vMinf3);
	vMax = XMLoadFloat3(&vMaxf3);
	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;

		XMVECTOR P = XMLoadFloat3(&cylinder.Vertices[i].Position);

		vMin = XMVectorMin(vMin, P);
		vMax = XMVectorMax(vMax, P);
	}

	BoundingBox cylinder_bounds;
	XMStoreFloat3(&cylinder_bounds.Center, 0.5f * (vMin + vMax));
	XMStoreFloat3(&cylinder_bounds.Extents, 0.5f * (vMax - vMin));
	cylinderSubmesh.Bounds = cylinder_bounds;

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
			2, (UINT)maxObjectNum, (UINT)matManager->GetSize(), mCommandList));
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
		0, 1, 
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), 
		XMFLOAT3(0.1f, 0.1f, 0.1f), 0.3f, 2);  // with normal_map

	matManager->CreateMaterial("tile0", 
		1, 3, 
		XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), 
		XMFLOAT3(0.2f, 0.2f, 0.2f), 0.1f, 4);

	matManager->CreateMaterial("mirror0",
		2, 5,
		XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f),
		XMFLOAT3(0.98f, 0.97f, 0.95f), 0.1f);

	matManager->CreateMaterial("brokenGlass0",
		3, 6,
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.727811f, 0.626959f, 0.626959f), 0.9f);

	matManager->CreateMaterial("sky",
		4, 8,
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.1f, 0.1f, 0.1f), 1.0f);

	matManager->CreateMaterial("marry",
		5, 7,
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
		XMFLOAT3(0.1f, 0.1f, 0.1f), 0.3f);
}

void ZeroRenderer::BuildRenderItems()
{
	auto general_geo = mGeometries["shapeGeo"].get();

	/*
		Args List:
			layer              : 该 Ritem 所属的 RenderLayer
			world              : 世界矩阵
			TexTransform       : 纹理变换矩阵
			Mat                : 该 Ritem 的材质
			Geo                : 该物体所属的 Mesh（其中包含顶点索引缓冲区和拓扑类型等，用于绘制时绑定）
			IndexCount         : Number of indices read from the index buffer for each instance
			StartIndexLocation : 在索引缓冲区中的 offset
			BaseVertexLocation : 在顶点缓冲区中的 offset
			PrimitiveType      : 该图元的拓扑类型 (默认为 TRIANGLELIST)
			Bounds
		每个 RItem 的 ObjCBIndex 按顺序生成 (0, 1, 2, ...)
	*/

	mScene->CreateRenderItem(
		RenderLayer::Sky,
		XMMatrixScaling(5000.0f, 5000.0f, 5000.0f),
		XMMatrixIdentity(),
		matManager->GetMaterial("sky"),
		general_geo,
		general_geo->DrawArgs["sphere"].IndexCount,
		general_geo->DrawArgs["sphere"].StartIndexLocation,
		general_geo->DrawArgs["sphere"].BaseVertexLocation,
		general_geo->DrawArgs["sphere"].Bounds
	);

	mScene->CreateRenderItem(
		RenderLayer::Opaque,
		XMMatrixScaling(3.0f, 3.0f, 3.0f),
		XMMatrixScaling(8.0f, 8.0f, 0.8f),
		matManager->GetMaterial("tile0"),
		general_geo,
		general_geo->DrawArgs["grid"].IndexCount,
		general_geo->DrawArgs["grid"].StartIndexLocation,
		general_geo->DrawArgs["grid"].BaseVertexLocation,
		general_geo->DrawArgs["grid"].Bounds
	);

	//mScene->CreateRenderItem(
	//	RenderLayer::Opaque,
	//	XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(0.0f, 6.0f, 0.0f),
	//	XMMatrixIdentity(),
	//	matManager->GetMaterial("mirror0"),
	//	general_geo,
	//	general_geo->DrawArgs["sphere"].IndexCount,
	//	general_geo->DrawArgs["sphere"].StartIndexLocation,
	//	general_geo->DrawArgs["sphere"].BaseVertexLocation,
	//	general_geo->DrawArgs["sphere"].Bounds
	//);

	//mScene->CreateRenderItem(
	//	RenderLayer::Opaque,
	//	XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(8.0f, 6.0f, 3.0f),
	//	XMMatrixIdentity(),
	//	matManager->GetMaterial("bricks0"),
	//	general_geo,
	//	general_geo->DrawArgs["box"].IndexCount,
	//	general_geo->DrawArgs["box"].StartIndexLocation,
	//	general_geo->DrawArgs["box"].BaseVertexLocation,
	//	general_geo->DrawArgs["box"].Bounds
	//);

	//mScene->CreateRenderItem(
	//	RenderLayer::Opaque,
	//	XMMatrixScaling(4.0f, 4.0f, 4.0f) * XMMatrixTranslation(16.0f, 6.0f, 6.0f),
	//	XMMatrixIdentity(),
	//	matManager->GetMaterial("tile0"),
	//	general_geo,
	//	general_geo->DrawArgs["box"].IndexCount,
	//	general_geo->DrawArgs["box"].StartIndexLocation,
	//	general_geo->DrawArgs["box"].BaseVertexLocation,
	//	general_geo->DrawArgs["box"].Bounds
	//);

	//mScene->CreateRenderItem(
	//	RenderLayer::Transparent,
	//	XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-8.0f, 6.0f, -3.0f),
	//	XMMatrixIdentity(),
	//	matManager->GetMaterial("brokenGlass0"),
	//	general_geo,
	//	general_geo->DrawArgs["sphere"].IndexCount,
	//	general_geo->DrawArgs["sphere"].StartIndexLocation,
	//	general_geo->DrawArgs["sphere"].BaseVertexLocation,
	//	general_geo->DrawArgs["sphere"].Bounds
	//);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ZeroRenderer::GetCpuSrv(int index)const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ZeroRenderer::GetGpuSrv(int index)const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, mCbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ZeroRenderer::GetDsv(int index)const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, mDsvDescriptorSize);
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ZeroRenderer::GetRtv(int index)const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, mRtvDescriptorSize);
	return rtv;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT ZeroRenderer::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// For ImGui
	if (ImGui_ImplWin32_WndProcHandler(mhMainWnd, msg, wParam, lParam))
		return true;

	return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void ZeroRenderer::Pick(int sx, int sy)
{
	//OutputDebugString(L"InPick\n");
	XMFLOAT4X4 P = mCamera.GetProj4x4f();

	// Compute picking ray in view space. (with Topleftx = 330.0f)
	float vx = (+2.0f * sx / mClientWidth - 1.0f - 660.f / mClientWidth) / P(0, 0);
	float vy = (-2.0f * sy / mClientHeight + 1.0f) / P(1, 1);

	// Ray definition in view space.
	XMVECTOR rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

	XMMATRIX V = mCamera.GetView();
	XMMATRIX invView = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(V)), V);

	if (mPickedRitem) mPickedRitem->Visible = false;
	
	for (const auto& ri : mScene->GetRenderLayer((int)RenderLayer::Opaque))
	{
		rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

		auto geo = ri->Geo;
		if (ri->Visible == false)
			continue;

		XMMATRIX W = XMLoadFloat4x4(&ri->World);
		XMMATRIX invWorld = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(W)), W);

		// Tranform ray to vi space of Mesh.
		XMMATRIX toLocal = XMMatrixMultiply(invView, invWorld);

		rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
		rayDir = XMVector3TransformNormal(rayDir, toLocal);

		// Make the ray direction unit length for the intersection tests.
		rayDir = XMVector3Normalize(rayDir);

		float tmin = 0.001f;
		if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
		{
			//OutputDebugString(L"InInnerPick\n");
			if (mPickedRitem) mPickedRitem->Visible = true;
			mPickedRitem = ri;
		}
	}

	for (const auto& ri : mScene->GetRenderLayer((int)RenderLayer::Transparent))
	{
		rayOrigin = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		rayDir = XMVectorSet(vx, vy, 1.0f, 0.0f);

		auto geo = ri->Geo;
		if (ri->Visible == false)
			continue;

		XMMATRIX W = XMLoadFloat4x4(&ri->World);
		XMMATRIX invWorld = XMMatrixInverse(get_rvalue_ptr(XMMatrixDeterminant(W)), W);

		// Tranform ray to vi space of Mesh.
		XMMATRIX toLocal = XMMatrixMultiply(invView, invWorld);

		rayOrigin = XMVector3TransformCoord(rayOrigin, toLocal);
		rayDir = XMVector3TransformNormal(rayDir, toLocal);

		// Make the ray direction unit length for the intersection tests.
		rayDir = XMVector3Normalize(rayDir);

		float tmin = 0.001f;
		if (ri->Bounds.Intersects(rayOrigin, rayDir, tmin))
		{
			//OutputDebugString(L"InInnerPick\n");
			if (mPickedRitem) mPickedRitem->Visible = true;
			mPickedRitem = ri;
		}
	}
}
