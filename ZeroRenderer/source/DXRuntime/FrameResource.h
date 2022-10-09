#pragma once

#include "../Common/d3dUtil.h"

#include "../Math/MathHelper.h"

#include "../Resource/UploadBuffer.h"

#include "CommandListHandle.h"

// render pass constants
struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProjTex = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ShadowTransform = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    Light Lights[MaxLights];
};

// object constants
struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();        // world matrix
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4(); // tex   matrix
    UINT     MaterialIndex;
    /* for alignment */
    UINT     ObjPad0;
    UINT     ObjPad1;
    UINT     ObjPad2;
};

// structed buffer content
struct MaterialData
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.5f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

	UINT DiffuseMapIndex = 0;  // tex index
    UINT NormalMapIndex = 0;

    /* data for alignment */
	UINT MaterialPad1;
	UINT MaterialPad2;
};

struct InstanceData
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    UINT MaterialIndex;
    UINT InstancePad0;
    UINT InstancePad1;
    UINT InstancePad2;
};

// vertex
struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
    DirectX::XMFLOAT3 TangentU;
};

struct SsaoConstants
{
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 InvProj;
    DirectX::XMFLOAT4X4 ProjTex;
    DirectX::XMFLOAT4   OffsetVectors[14];

    // For SsaoBlur.hlsl
    DirectX::XMFLOAT4 BlurWeights[3];

    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };

    // Coordinates given in view space.
    float OcclusionRadius = 0.5f;
    float OcclusionFadeStart = 0.2f;
    float OcclusionFadeEnd = 2.0f;
    float SurfaceEpsilon = 0.05f;
};

class FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, 
        UINT materialCount, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList,
        UINT maxInstanceCount = 25);

    // ½ûÖ¹¿½±´
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;

    ~FrameResource();

    CommandListHandle Command();

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CmdList;

    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;

    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;

    std::unique_ptr<UploadBuffer<InstanceData>> InstanceBuffer = nullptr;

    std::unique_ptr<UploadBuffer<SsaoConstants>> SsaoCB = nullptr;

    UINT64 Fence = 0;  // for sync
};
