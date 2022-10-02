#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, 
                             UINT materialCount, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList,
                             UINT maxInstanceCount) : CmdList(cmdList)
{
    // create the own commandAllocator
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

    PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);

    ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);

    //For Instancing
    MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, maxInstanceCount, false);
}

FrameResource::~FrameResource()
{

}

CommandListHandle FrameResource::Command()
{
    return { CmdListAlloc.Get(), CmdList.Get() };
}
