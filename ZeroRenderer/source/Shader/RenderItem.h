#pragma once

#include <DirectXMath.h>

#include "../Common/d3dUtil.h"
#include "../Common/MathHelper.h"

#include "../Resource/Mesh.h"

#include "MatManager.h"

using namespace DirectX;

enum class RenderLayer : int
{
	Opaque = 0,
	Sky,
	Transparent,
	Debug,
	Count
};

// Lightweight structure stores parameters to draw a shape..
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};
