#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <queue>

namespace ACG {

// Forward declarations
class Texture;

/**
 * @brief Virtual Texture Tile Information
 * Represents a single tile in the virtual texture system
 */
struct VirtualTextureTile {
    uint32_t textureIndex;      // Which texture this tile belongs to
    uint32_t mipLevel;          // Mip level of this tile
    uint32_t tileX;             // Tile X coordinate
    uint32_t tileY;             // Tile Y coordinate
    bool isResident;            // Is this tile currently in GPU memory
    uint32_t physicalPageIndex; // Index in physical memory pool
};

/**
 * @brief Virtual Texture Atlas Configuration
 */
struct VirtualTextureConfig {
    uint32_t tileSize = 256;              // Size of each tile (256x256 is standard)
    uint32_t maxPhysicalPages = 4096;     // Maximum physical memory pages (256MB at 256x256 RGBA)
    uint32_t maxVirtualTextures = 1024;   // Maximum number of virtual textures
    uint32_t feedbackBufferSize = 1024;   // Size of feedback buffer for streaming
    bool enableTiledResources = true;     // Use DX12 tiled resources
    bool enableSparseBinding = true;      // Use sparse binding for better memory efficiency
};

/**
 * @brief Virtual Texture System
 * Implements virtual texturing using DX12 Tiled Resources (Reserved Resources)
 * Industry-standard solution for massive texture sets
 */
class VirtualTextureSystem {
public:
    VirtualTextureSystem();
    ~VirtualTextureSystem();

    // Initialize the virtual texture system
    bool Initialize(ID3D12Device* device, const VirtualTextureConfig& config);
    
    // Check if tiled resources are supported
    bool CheckTiledResourcesSupport(ID3D12Device* device);
    
    // Add a texture to the virtual texture system
    // Returns virtual texture index
    int32_t AddVirtualTexture(const std::shared_ptr<Texture>& texture);
    
    // Upload all tiles for all textures (after all textures added)
    bool UploadAllTiles(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue);
    
    // Create indirection texture for shader access
    bool CreateIndirectionTexture(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue);
    
    // Upload texture data for a specific tile
    void UploadTile(ID3D12GraphicsCommandList* cmdList, 
                    uint32_t virtualTextureIndex,
                    uint32_t mipLevel,
                    uint32_t tileX,
                    uint32_t tileY,
                    const void* data,
                    size_t dataSize);
    
    // Make a tile resident in GPU memory
    bool MakeTileResident(ID3D12CommandQueue* commandQueue,
                         uint32_t virtualTextureIndex,
                         uint32_t mipLevel,
                         uint32_t tileX,
                         uint32_t tileY);
    
    // Evict a tile from GPU memory
    void EvictTile(ID3D12CommandQueue* commandQueue,
                   uint32_t virtualTextureIndex,
                   uint32_t mipLevel,
                   uint32_t tileX,
                   uint32_t tileY);
    
    // Create SRV for shader access
    void CreateShaderResourceView(ID3D12Device* device,
                                  D3D12_CPU_DESCRIPTOR_HANDLE srvHandle,
                                  D3D12_CPU_DESCRIPTOR_HANDLE indirectionSrvHandle);
    
    // Get virtual texture resources for binding
    ID3D12Resource* GetVirtualTexture(uint32_t index) const {
        return (index < m_virtualTextures.size()) ? m_virtualTextures[index].Get() : nullptr;
    }
    
    ID3D12Resource* GetIndirectionTexture() const {
        return m_indirectionTexture.Get();
    }
    
    // Get virtual texture info for shader
    struct VirtualTextureInfo {
        uint32_t tileSize;
        uint32_t numTilesX;
        uint32_t numTilesY;
        uint32_t physicalPageIndex;
    };
    VirtualTextureInfo GetTextureInfo(uint32_t virtualTextureIndex) const;
    
    // Statistics structure
    struct Statistics {
        uint32_t numVirtualTextures;
        uint32_t totalPhysicalPages;
        uint32_t usedPhysicalPages;
        uint64_t totalVirtualMemoryMB;
        uint64_t physicalMemoryMB;
    };
    Statistics GetStatistics() const;
    
    // Feedback-based streaming (for future optimization)
    void ProcessFeedback(const void* feedbackData, size_t dataSize);
    
private:
    // DX12 Resources
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12Heap> m_physicalMemoryHeap;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_virtualTextures;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_physicalCacheTexture;  // Single large texture for all physical pages
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indirectionTexture;  // Maps virtual coords to physical tiles
    
    // Virtual texture metadata
    struct VirtualTextureMetadata {
        uint32_t width;
        uint32_t height;
        uint32_t numMipLevels;
        uint32_t numTilesX;
        uint32_t numTilesY;
        std::vector<VirtualTextureTile> tiles;
        std::shared_ptr<Texture> sourceTexture;
    };
    std::vector<VirtualTextureMetadata> m_virtualTextureMetadata;
    
    // Physical memory management
    struct PhysicalPage {
        bool isAllocated;
        uint32_t virtualTextureIndex;
        uint32_t mipLevel;
        uint32_t tileX;
        uint32_t tileY;
    };
    std::vector<PhysicalPage> m_physicalPages;
    std::queue<uint32_t> m_freePhysicalPages;
    
    // Configuration
    VirtualTextureConfig m_config;
    
    // Tiled resource support flags
    D3D12_TILED_RESOURCES_TIER m_tiledResourceTier;
    bool m_supportsTiledResources;
    
    // Helper functions
    uint32_t AllocatePhysicalPage();
    void FreePhysicalPage(uint32_t pageIndex);
    uint32_t CalculateNumTiles(uint32_t dimension, uint32_t tileSize) const;
    void CreateTiledResource(uint32_t width, uint32_t height, uint32_t mipLevels);
    
    // Upload buffer management (kept alive until GPU finishes)
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_uploadBuffers;
};

} // namespace ACG
