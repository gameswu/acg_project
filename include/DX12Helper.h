#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "d3dx12.h" // Include the D3D12 helper header

// For Agility SDK
// For Agility SDK
extern "C" { extern const UINT D3D12SDKVersion; }
extern "C" { extern const char* D3D12SDKPath; }

namespace ACG {
    // Helper for COM exceptions
    class com_exception : public std::runtime_error {
    public:
        com_exception(HRESULT hr, const std::string& msg) : std::runtime_error(msg), m_hr(hr) {}
        HRESULT get_result() const { return m_hr; }
    private:
        const HRESULT m_hr;
    };

    // Helper to throw exceptions on failed HRESULT
    inline void ThrowIfFailed(HRESULT hr, const std::string& msg = "") {
        if (FAILED(hr)) {
            throw com_exception(hr, msg);
        }
    }

    // Helper to get a hardware adapter
    inline Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp) {
        Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
        UINT createFactoryFlags = 0;
#if defined(_DEBUG)
        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
        ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

        if (useWarp) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> warpAdapter;
            ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
            Microsoft::WRL::ComPtr<IDXGIAdapter4> warpAdapter4;
            ThrowIfFailed(warpAdapter.As(&warpAdapter4));
            return warpAdapter4;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &hardwareAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            hardwareAdapter->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
                if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
                    Microsoft::WRL::ComPtr<IDXGIAdapter4> hardwareAdapter4;
                    ThrowIfFailed(hardwareAdapter.As(&hardwareAdapter4));
                    return hardwareAdapter4;
                }
            }
        }
        return nullptr;
    }

    // Helper to create a default buffer
    inline Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const void* initData,
        UINT64 byteSize,
        Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer) {

        Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer;

        // Create the actual default buffer resource.
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

        // In order to copy CPU memory data into our default buffer, we need to create
        // an intermediate upload heap.
        auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

        // Describe the data we want to copy into the default buffer.
        D3D12_SUBRESOURCE_DATA subResourceData = {};
        subResourceData.pData = initData;
        subResourceData.RowPitch = byteSize;
        subResourceData.SlicePitch = subResourceData.RowPitch;

        // Schedule a copy from the upload buffer to the default buffer.
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->ResourceBarrier(1, &barrier);

        UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

        auto transition = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
        cmdList->ResourceBarrier(1, &transition);

        // Note: uploadBuffer has to be kept alive after the above function calls because
        // the command list has not been executed yet that performs the actual copy.
        // The caller can Release the uploadBuffer after it knows the copy has been executed.
        return defaultBuffer;
    }
}
