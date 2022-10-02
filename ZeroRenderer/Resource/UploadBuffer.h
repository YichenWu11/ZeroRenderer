#pragma once

#include "../Common/d3dUtil.h"

template<typename T>
class UploadBuffer
{
public:
    // 可用于各种类型的上传缓冲区
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
        mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        if (isConstantBuffer)
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));

        ThrowIfFailed(device->CreateCommittedResource(
            get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
            D3D12_HEAP_FLAG_NONE,
            get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount)),
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
            mUploadBuffer->Unmap(0, nullptr);

        mMappedData = nullptr;
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer.Get();
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;  // map media

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
