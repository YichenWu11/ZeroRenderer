#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

// ����һ���ڴ�����
ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

// ���������� Tool Fuction
Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;


    // CD3DX12_RESOURCE_DESC ---> һ�ּ򻯻������������̵� D3D12_RESOURCE_DESC �Ĺ��캯��
    // ���в��� bytesize ��ʾ����������ռ���ֽ���
    
    // Create the actual default buffer resource.
    ThrowIfFailed(device->CreateCommittedResource(
        // ����һ��Ĭ�϶ѣ�ֻ�� GPU ��Ҫ�����ж�ȡ���������Ƽ�����
        get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
        D3D12_HEAP_FLAG_NONE,
        get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(byteSize)),
		D3D12_RESOURCE_STATE_COMMON,   // ��ʼĬ��״̬
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 
    // Ϊ�˽�CPU���ڴ��е����ݸ��Ƶ�Ĭ�ϻ����������ǻ���Ҫ����һ��λ���н�λ�õ��ϴ���
    ThrowIfFailed(device->CreateCommittedResource(
        get_rvalue_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)), // �ϴ���
		D3D12_HEAP_FLAG_NONE,
        get_rvalue_ptr(CD3DX12_RESOURCE_DESC::Buffer(byteSize)), // ��С��ղŴ�����Ĭ�϶���ͬ
		D3D12_RESOURCE_STATE_GENERIC_READ,  
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // Describe the data we want to copy into the default buffer.
    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    // ���������������ڻ��������ԣ���Ϊ��Ҫ�������ݵ��ֽ���
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    // �����ݸ��Ƶ�Ĭ�ϻ�������Դ������
    // UpdateSubresources �����������Ȱ����ݴ� CPU �˵��ڴ��и��Ƶ�λ���н�λ�õ��ϴ�����
    // Ȼ����ͨ������ ID3D12CommandList::CopySubresourceRegion ���������ϴ����ڵ����ݸ���
    // �� defaultBuffer ��
	cmdList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)));
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	cmdList->ResourceBarrier(1, get_rvalue_ptr(CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ)));

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.


    return defaultBuffer;
}

// ����һ���ڴ�����, ��Ҫ��֮���ֶ�ת�� (Byte*) �ֽ���
ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION; // ����ģʽ | ָʾ�����������Ż��׶�
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    // ��������Ϣ��������Դ���
	if(errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}

// output error msg when catch the exception
std::wstring DxException::ToString()const
{
    // Get the string description of the error code.
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

