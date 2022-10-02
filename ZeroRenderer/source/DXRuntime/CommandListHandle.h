#pragma once

#include <d3d12.h>

#include "../Common/d3dUtil.h"

class CommandListHandle {
	ID3D12GraphicsCommandList* cmdList;

public:
	CommandListHandle(CommandListHandle&&);

	CommandListHandle(CommandListHandle const&) = delete;

	ID3D12GraphicsCommandList* CmdList() const { return cmdList; }

	CommandListHandle(
		ID3D12CommandAllocator* allocator,
		ID3D12GraphicsCommandList* cmdList);

	~CommandListHandle();
};

