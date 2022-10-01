#pragma once

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/Camera.h"
#include "../Common/GeometryGenerator.h"

#include "../DXRuntime/FrameResource.h"

#include "../Shader/GlobalSamplers.h"
#include "../Shader/PSOManager.h"
#include "../Shader/RenderItem.h"
#include "../Shader/ShadowMap.h"
#include "../Shader/MatManager.h"
#include "../Shader/ShaderManager.h"

#include "Scene.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

class ZeroRenderer final : public D3DApp
{
public:
	ZeroRenderer(HINSTANCE hInstance);
    ZeroRenderer(const ZeroRenderer&) = delete;
    ZeroRenderer& operator=(const ZeroRenderer&) = delete;
    ~ZeroRenderer();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void CreateRtvAndDsvDescriptorHeaps() override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateShadowTransform(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateShadowPassCB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void DrawSceneToShadowMap();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr; 

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    std::unique_ptr<Scene>         mScene;

    std::unique_ptr<PSOManager>    psoManager;
    std::unique_ptr<MatManager>    matManager;
    std::unique_ptr<ShaderManager> shaderManager;

    PassConstants mMainPassCB;      // index 0 of pass cbuffer.
    PassConstants mShadowPassCB;    // index 1 of pass cbuffer.

    UINT mSkyTexHeapIndex = 0;      // skybox index in srv heap
    UINT mShadowMapHeapIndex = 0;

    UINT mNullCubeSrvIndex = 0;
    UINT mNullTexSrvIndex = 0;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera mCamera;

    POINT mLastMousePos;

    bool mIsWireframe = false;

    std::unique_ptr<ShadowMap> mShadowMap;

    DirectX::BoundingSphere mSceneBounds;

    float mLightNearZ = 0.0f;
    float mLightFarZ = 0.0f;
    XMFLOAT3 mLightPosW;
    XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
    XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
    XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

    float mLightRotationAngle = 0.0f;
    XMFLOAT3 mBaseLightDirections[3] = {
        XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        XMFLOAT3(0.0f, -0.707f, -0.707f)
    };
    XMFLOAT3 mRotatedLightDirections[3];
};
