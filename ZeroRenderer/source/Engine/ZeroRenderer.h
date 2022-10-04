#pragma once

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/Camera.h"
#include "../Common/GeometryGenerator.h"

#include "../DXRuntime/FrameResource.h"
#include "../DXRuntime/CommandListHandle.h"

#include "../Resource/UploadBuffer.h"
#include "../Resource/Mesh.h"

#include "../Shader/GlobalSamplers.h"
#include "../Shader/PSOManager.h"
#include "../Shader/RenderItem.h"
#include "../Shader/ShadowMap.h"
#include "../Shader/Ssao.h"
#include "../Shader/MatManager.h"
#include "../Shader/ShaderManager.h"

#include "Scene.h"
#include "ShadowPass.h"
#include "SsaoPass.h"
#include "MainPass.h"

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

    void LoadTextures();
    void BuildRootSignature();
    void BuildSsaoRootSignature();
    void BuildDescriptorHeaps();
    void BuildShapeGeometry();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();

    void PopulateCommandList(const GameTimer& gt);
    void SubmitCommandList(const GameTimer& gt);

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index) const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index) const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index)    const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index)    const;
private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    std::unique_ptr<Scene>         mScene;

    std::unique_ptr<PSOManager>    psoManager;
    std::unique_ptr<MatManager>    matManager;
    std::unique_ptr<ShaderManager> shaderManager;

    UINT mSkyTexHeapIndex = 0;      // skybox index in srv heap
    UINT mShadowMapHeapIndex = 0;
    UINT mSsaoHeapIndexStart = 0;
    UINT mSsaoAmbientMapIndex = 0;

    UINT mNullCubeSrvIndex = 0;
    UINT mNullTexSrvIndex = 0;
    UINT mNullTexSrvIndex1 = 0;
    UINT mNullTexSrvIndex2 = 0;

    CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

    Camera mCamera;

    POINT mLastMousePos;

    std::unique_ptr<ShadowPass> shadowPass;
    std::unique_ptr<SsaoPass>   ssaoPass;
    std::unique_ptr<MainPass>   mainPass;
};
