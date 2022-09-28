#pragma once

#include "d3dUtil.h"

template<typename T>
class UploadBuffer
{
public:
    // 可用于各种类型的上传缓冲区
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
        mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        // Constant buffer elements need to be multiples of 256 bytes.
        // This is because the hardware can only view constant data 
        // at m*256 byte offsets and of n*256 byte lengths. 
        // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
        // UINT64 OffsetInBytes; // multiple of 256
        // UINT   SizeInBytes;   // multiple of 256
        // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
        if (isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

        // 创建一个资源和一个堆，并将资源提交到堆中
        ThrowIfFailed(device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)), // 上传堆
            D3D12_HEAP_FLAG_NONE,
            get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount)), // elementCount个
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)));

        // 通过 CPU（mMappedData）为常量缓冲区资源更新数据
        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // 只要还会修改当前的资源，我们就无须取消映射
        // 但是，在资源被GPU使用期间，我们千万不可向该资源进行写操作（所以必须借助于同步技术）
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (mUploadBuffer != nullptr)
            // 在释放映射内存之前对其进行取消映射操作
            mUploadBuffer->Unmap(0, nullptr);

        mMappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();  // 获取上传缓冲区资源
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer; // 上传缓冲区
    BYTE* mMappedData = nullptr;  // map media

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
