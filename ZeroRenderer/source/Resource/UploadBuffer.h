#pragma once

#include "../Common/d3dUtil.h"

template<typename T>
class UploadBuffer
{
public:
    // �����ڸ������͵��ϴ�������
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

        // ͨ�� CPU��mMappedData��Ϊ������������Դ��������
        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        // ֻҪ�����޸ĵ�ǰ����Դ�����Ǿ�����ȡ��ӳ��
        // ���ǣ�����Դ��GPUʹ���ڼ䣬����ǧ�򲻿������Դ����д���������Ա��������ͬ��������
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
