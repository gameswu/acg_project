#include "VirtualTextureSystem.h"
#include "Texture.h"
#include "DX12Helper.h"
#include <iostream>
#include <algorithm>

namespace ACG {

VirtualTextureSystem::VirtualTextureSystem()
    : m_supportsTiledResources(false)
    , m_tiledResourceTier(D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED)
{
}

VirtualTextureSystem::~VirtualTextureSystem() {
    // Cleanup resources
}

bool VirtualTextureSystem::CheckTiledResourcesSupport(ID3D12Device* device) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    HRESULT hr = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS,
        &options,
        sizeof(options)
    );
    
    if (SUCCEEDED(hr)) {
        m_tiledResourceTier = options.TiledResourcesTier;
        m_supportsTiledResources = (m_tiledResourceTier >= D3D12_TILED_RESOURCES_TIER_1);
        
        std::cout << "[Virtual Texture] Tiled Resources Support: ";
        switch (m_tiledResourceTier) {
            case D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED:
                std::cout << "NOT SUPPORTED" << std::endl;
                break;
            case D3D12_TILED_RESOURCES_TIER_1:
                std::cout << "TIER 1 (Basic tiled resources)" << std::endl;
                break;
            case D3D12_TILED_RESOURCES_TIER_2:
                std::cout << "TIER 2 (Non-power-of-2 textures)" << std::endl;
                break;
            case D3D12_TILED_RESOURCES_TIER_3:
                std::cout << "TIER 3 (Volume textures)" << std::endl;
                break;
            case D3D12_TILED_RESOURCES_TIER_4:
                std::cout << "TIER 4 (64KB standard swizzle)" << std::endl;
                break;
        }
        
        return m_supportsTiledResources;
    }
    
    return false;
}

bool VirtualTextureSystem::Initialize(ID3D12Device* device, const VirtualTextureConfig& config) {
    m_device = device;
    m_config = config;
    
    std::cout << "[Virtual Texture] Initializing Virtual Texture System..." << std::endl;
    std::cout << "  Tile Size: " << config.tileSize << "x" << config.tileSize << std::endl;
    std::cout << "  Max Physical Pages: " << config.maxPhysicalPages << std::endl;
    std::cout << "  Max Virtual Textures: " << config.maxVirtualTextures << std::endl;
    
    // Check tiled resources support
    if (!CheckTiledResourcesSupport(device)) {
        std::cerr << "[Virtual Texture] ✗ ERROR: Tiled Resources not supported on this GPU" << std::endl;
        std::cerr << "  Falling back to conventional texture atlas" << std::endl;
        return false;
    }
    
    // Calculate physical memory heap size
    // Tile size: 256x256 RGBA8 = 256KB per tile (with padding to 64KB alignment)
    const uint32_t tileSize = 65536; // 64KB standard tile size
    uint64_t heapSize = static_cast<uint64_t>(config.maxPhysicalPages) * tileSize;
    
    std::cout << "  Physical Memory Heap: " << (heapSize / 1024 / 1024) << " MB" << std::endl;
    
    // Create physical memory heap for tiled resources
    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = heapSize;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    
    HRESULT hr = device->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_physicalMemoryHeap));
    if (FAILED(hr)) {
        std::cerr << "[Virtual Texture] ✗ ERROR: Failed to create physical memory heap (HRESULT: 0x" 
                  << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }
    
    // Initialize physical page pool
    m_physicalPages.resize(config.maxPhysicalPages);
    for (uint32_t i = 0; i < config.maxPhysicalPages; ++i) {
        m_physicalPages[i].isAllocated = false;
        m_freePhysicalPages.push(i);
    }
    
    // Create physical cache texture: large texture to hold all physical pages
    // Use 48x48 grid for 2304 pages (enough for most scenes, less than 64x64 for 4096)
    // Each page is 256x256 pixels
    // Total size: 12288x12288 pixels = 576MB (reasonable memory usage)
    const uint32_t CACHE_TILES_PER_ROW = 48;  // sqrt(2304) = 48
    const uint32_t actualCachePages = std::min(config.maxPhysicalPages, static_cast<uint32_t>(2304));
    const uint32_t cacheTextureSize = CACHE_TILES_PER_ROW * config.tileSize;  // 48 * 256 = 12288
    
    std::cout << "  Creating physical cache for " << actualCachePages << " pages (" 
              << CACHE_TILES_PER_ROW << "x" << CACHE_TILES_PER_ROW << " grid)" << std::endl;
    
    D3D12_RESOURCE_DESC cacheDesc = {};
    cacheDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    cacheDesc.Width = cacheTextureSize;
    cacheDesc.Height = cacheTextureSize;
    cacheDesc.DepthOrArraySize = 1;
    cacheDesc.MipLevels = 1;
    cacheDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cacheDesc.SampleDesc.Count = 1;
    cacheDesc.SampleDesc.Quality = 0;
    cacheDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    cacheDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    CD3DX12_HEAP_PROPERTIES cacheHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    hr = m_device->CreateCommittedResource(
        &cacheHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &cacheDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,  // Start in COPY_DEST to match first barrier
        nullptr,
        IID_PPV_ARGS(&m_physicalCacheTexture)
    );
    
    if (FAILED(hr)) {
        std::cerr << "[Virtual Texture] ✗ ERROR: Failed to create physical cache texture (HRESULT: 0x" 
                  << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }
    
    m_physicalCacheTexture->SetName(L"Virtual Texture Physical Cache");
    std::cout << "  Physical Cache Texture: " << cacheTextureSize << "x" << cacheTextureSize << " pixels" << std::endl;
    
    // Reserve space for virtual textures
    m_virtualTextures.reserve(config.maxVirtualTextures);
    m_virtualTextureMetadata.reserve(config.maxVirtualTextures);
    
    std::cout << "[Virtual Texture] ✓ Virtual Texture System initialized successfully" << std::endl;
    return true;
}

void VirtualTextureSystem::CreateTiledResource(uint32_t width, uint32_t height, uint32_t mipLevels) {
    // Create a reserved (tiled) resource
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = mipLevels;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE; // Required for tiled resources
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> tiledResource;
    
    // Create reserved resource (virtual allocation, no physical memory yet)
    HRESULT hr = m_device->CreateReservedResource(
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&tiledResource)
    );
    
    if (SUCCEEDED(hr)) {
        m_virtualTextures.push_back(tiledResource);
    } else {
        throw std::runtime_error("Failed to create tiled resource");
    }
}

int32_t VirtualTextureSystem::AddVirtualTexture(const std::shared_ptr<Texture>& texture) {
    if (m_virtualTextures.size() >= m_config.maxVirtualTextures) {
        std::cerr << "[Virtual Texture] ✗ ERROR: Maximum virtual textures reached" << std::endl;
        return -1;
    }
    
    uint32_t width = texture->GetWidth();
    uint32_t height = texture->GetHeight();
    
    std::cout << "[Virtual Texture] Adding texture " << m_virtualTextures.size() 
              << ": " << width << "x" << height << std::endl;
    
    // Calculate number of mip levels (for future LOD support)
    uint32_t mipLevels = 1; // Start with base level only
    
    // Create tiled resource
    try {
        CreateTiledResource(width, height, mipLevels);
    } catch (const std::exception& e) {
        std::cerr << "[Virtual Texture] ✗ Failed to create tiled resource: " << e.what() << std::endl;
        return -1;
    }
    
    // Calculate tile layout
    uint32_t numTilesX = CalculateNumTiles(width, m_config.tileSize);
    uint32_t numTilesY = CalculateNumTiles(height, m_config.tileSize);
    uint32_t totalTiles = numTilesX * numTilesY;
    
    std::cout << "  Tile Layout: " << numTilesX << "x" << numTilesY 
              << " = " << totalTiles << " tiles" << std::endl;
    
    // Create metadata
    VirtualTextureMetadata metadata;
    metadata.width = width;
    metadata.height = height;
    metadata.numMipLevels = mipLevels;
    metadata.numTilesX = numTilesX;
    metadata.numTilesY = numTilesY;
    metadata.sourceTexture = texture;
    
    // Initialize tiles
    metadata.tiles.reserve(totalTiles);
    for (uint32_t y = 0; y < numTilesY; ++y) {
        for (uint32_t x = 0; x < numTilesX; ++x) {
            VirtualTextureTile tile;
            tile.textureIndex = static_cast<uint32_t>(m_virtualTextures.size() - 1);
            tile.mipLevel = 0;
            tile.tileX = x;
            tile.tileY = y;
            tile.isResident = false;
            tile.physicalPageIndex = UINT32_MAX;
            metadata.tiles.push_back(tile);
        }
    }
    
    m_virtualTextureMetadata.push_back(metadata);
    
    return static_cast<int32_t>(m_virtualTextures.size() - 1);
}

bool VirtualTextureSystem::UploadAllTiles(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue, size_t tileBatchSize) {
    std::cout << "[Virtual Texture] Uploading all tiles to GPU (batch size: " << tileBatchSize << ")..." << std::endl;
    
    // Clear any previous upload buffers
    m_uploadBuffers.clear();
    
    // Create our own command allocator and list for batched uploads
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> batchAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> batchCmdList;
    
    HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&batchAllocator));
    if (FAILED(hr)) {
        std::cerr << "  ✗ Failed to create command allocator for upload" << std::endl;
        return false;
    }
    
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, batchAllocator.Get(), nullptr, IID_PPV_ARGS(&batchCmdList));
    if (FAILED(hr)) {
        std::cerr << "  ✗ Failed to create command list for upload" << std::endl;
        return false;
    }
    
    // Create fence for synchronization
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    
    size_t totalTilesUploaded = 0;
    const size_t MAX_TILES_PER_BATCH = tileBatchSize;  // Execute GPU commands based on user setting
    std::vector<size_t> texturesInCurrentBatch;  // Track textures that need state transition back
    
    // Create a shared upload buffer for batched tile uploads (reduces memory pressure)
    const size_t TILE_DATA_SIZE = m_config.tileSize * m_config.tileSize * 4;  // 256x256x4 = 256KB per tile
    const size_t BATCH_UPLOAD_BUFFER_SIZE = TILE_DATA_SIZE * MAX_TILES_PER_BATCH;  // e.g., 50 tiles = 12.5MB
    Microsoft::WRL::ComPtr<ID3D12Resource> sharedUploadBuffer;
    size_t currentTileInBatch = 0;  // Track offset within current batch upload buffer
    
    // Create the shared upload buffer
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    
    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Alignment = 0;
    uploadBufferDesc.Width = BATCH_UPLOAD_BUFFER_SIZE;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.SampleDesc.Quality = 0;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    hr = m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&sharedUploadBuffer));
    
    if (FAILED(hr)) {
        std::cerr << "  ✗ Failed to create shared upload buffer (" << (BATCH_UPLOAD_BUFFER_SIZE / 1024.0 / 1024.0) << " MB)" << std::endl;
        return false;
    }
    
    std::cout << "  Created shared upload buffer: " << (BATCH_UPLOAD_BUFFER_SIZE / 1024.0 / 1024.0) << " MB for " << MAX_TILES_PER_BATCH << " tiles" << std::endl;
    
    // Persistent map the upload buffer once - keep it mapped throughout all uploads
    BYTE* pSharedBufferData = nullptr;
    D3D12_RANGE readRangeNone = { 0, 0 };  // We won't read from this resource on the CPU
    hr = sharedUploadBuffer->Map(0, &readRangeNone, reinterpret_cast<void**>(&pSharedBufferData));
    if (FAILED(hr)) {
        std::cerr << "  ✗ Failed to map shared upload buffer" << std::endl;
        CloseHandle(fenceEvent);
        return false;
    }
    
    // Transition physical cache to COPY_DEST state once at the beginning
    // NOTE: Physical cache was created in COPY_DEST state, so we start from there
    // No barrier needed here since it's already in the correct state
    
    // NOTE: We don't clear the physical cache to avoid state tracking issues.
    // The physical cache texture is created in COPY_DEST state and stays in that state.
    // Uninitialized cache positions will contain undefined data (black/zero from DEFAULT heap),
    // but all tiles we upload will have valid data.
    // The indirection texture marks which tiles are resident (not 0xFFFFFFFF),
    // so shader won't sample uninitialized areas.
    
    std::cout << "  Beginning tile uploads..." << std::endl;
    for (size_t texIdx = 0; texIdx < m_virtualTextureMetadata.size(); ++texIdx) {
        auto& metadata = m_virtualTextureMetadata[texIdx];
        auto& sourceTexture = metadata.sourceTexture;
        
        if (!sourceTexture || sourceTexture->GetWidth() == 0) {
            std::cout << "  Skipping texture " << texIdx << ": invalid source" << std::endl;
            continue;
        }
        
        const unsigned char* srcData = sourceTexture->GetRawData();
        if (!srcData) {
            std::cerr << "  ✗ Texture " << texIdx << ": null source data" << std::endl;
            continue;
        }
        
        int srcChannels = sourceTexture->GetChannels();
        int srcWidth = sourceTexture->GetWidth();
        int srcHeight = sourceTexture->GetHeight();
        size_t srcDataSize = static_cast<size_t>(srcWidth) * srcHeight * srcChannels;
        
        std::cout << "  Uploading texture " << texIdx << ": " << srcWidth << "x" << srcHeight 
                  << " (" << metadata.tiles.size() << " tiles)" << std::endl;
        
        // No need for per-texture state transition anymore since we upload to physical cache
        // Physical cache state transition will be done once at the beginning
        
        // Upload each tile
        for (auto& tile : metadata.tiles) {
            // Calculate tile source region
            uint32_t tileStartX = tile.tileX * m_config.tileSize;
            uint32_t tileStartY = tile.tileY * m_config.tileSize;
            uint32_t tileEndX = std::min(tileStartX + m_config.tileSize, static_cast<uint32_t>(srcWidth));
            uint32_t tileEndY = std::min(tileStartY + m_config.tileSize, static_cast<uint32_t>(srcHeight));
            uint32_t tileActualWidth = tileEndX - tileStartX;
            uint32_t tileActualHeight = tileEndY - tileStartY;
            
            // Allocate physical page
            uint32_t physicalPageIndex = AllocatePhysicalPage();
            if (physicalPageIndex == UINT32_MAX) {
                std::cerr << "    ✗ Out of physical memory for tile " << tile.tileX << "," << tile.tileY << std::endl;
                if (sharedUploadBuffer) {
                    sharedUploadBuffer->Unmap(0, nullptr);
                }
                CloseHandle(fenceEvent);
                return false;
            }
            
            // Note: We no longer use UpdateTileMappings since we're uploading directly to physical cache texture
            // instead of using tiled resources
            
            // Prepare tile data (convert to RGBA and pad to tile size)
            std::vector<BYTE> tileData(m_config.tileSize * m_config.tileSize * 4, 0);
            
            for (uint32_t y = 0; y < tileActualHeight; ++y) {
                for (uint32_t x = 0; x < tileActualWidth; ++x) {
                    uint32_t srcX = tileStartX + x;
                    uint32_t srcY = tileStartY + y;
                    
                    // Boundary check
                    if (srcX >= static_cast<uint32_t>(srcWidth) || srcY >= static_cast<uint32_t>(srcHeight)) {
                        continue;
                    }
                    
                    size_t srcIdx = static_cast<size_t>(srcY * srcWidth + srcX) * srcChannels;
                    size_t dstIdx = static_cast<size_t>(y * m_config.tileSize + x) * 4;
                    
                    // Extra safety check
                    if (srcIdx + srcChannels > srcDataSize) {
                        continue;
                    }
                    
                    if (srcChannels >= 3) {
                        tileData[dstIdx + 0] = srcData[srcIdx + 0];
                        tileData[dstIdx + 1] = srcData[srcIdx + 1];
                        tileData[dstIdx + 2] = srcData[srcIdx + 2];
                        tileData[dstIdx + 3] = (srcChannels == 4) ? srcData[srcIdx + 3] : 255;
                    } else if (srcChannels == 1) {
                        tileData[dstIdx + 0] = srcData[srcIdx];
                        tileData[dstIdx + 1] = srcData[srcIdx];
                        tileData[dstIdx + 2] = srcData[srcIdx];
                        tileData[dstIdx + 3] = 255;
                    }
                }
            }
            
            // DEBUG: Check tile data for first tile
            if (texIdx == 0 && tile.tileX == 0 && tile.tileY == 0) {
                uint32_t nonBlackPixels = 0;
                for (size_t i = 0; i < tileData.size(); i += 4) {
                    if (tileData[i] > 10 || tileData[i+1] > 10 || tileData[i+2] > 10) {
                        nonBlackPixels++;
                    }
                }
                std::cout << "    DEBUG: Texture 0 Tile[0,0] has " << nonBlackPixels << "/" << (tileData.size()/4) 
                          << " non-black pixels, first pixel=(" << (int)tileData[0] << "," << (int)tileData[1] << "," << (int)tileData[2] << ")" << std::endl;
            }
            
            // Calculate offset in current shared upload buffer
            size_t tileOffsetInBuffer = currentTileInBatch * TILE_DATA_SIZE;
            
            // Copy tile data to persistently mapped buffer (no need to Map/Unmap each time)
            memcpy(pSharedBufferData + tileOffsetInBuffer, tileData.data(), tileData.size());
            
            // Use CopyTextureRegion for tiled resources
            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.pResource = sharedUploadBuffer.Get();
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLocation.PlacedFootprint.Offset = tileOffsetInBuffer;
            srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            // CRITICAL: Set footprint dimensions to match the ACTUAL tile size in the buffer
            // The buffer contains a full 256x256 tile (padded), so footprint must be 256x256
            srcLocation.PlacedFootprint.Footprint.Width = m_config.tileSize;
            srcLocation.PlacedFootprint.Footprint.Height = m_config.tileSize;
            srcLocation.PlacedFootprint.Footprint.Depth = 1;
            srcLocation.PlacedFootprint.Footprint.RowPitch = m_config.tileSize * 4;
            
            D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
            dstLocation.pResource = m_physicalCacheTexture.Get();  // Upload to physical cache, not virtual texture
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLocation.SubresourceIndex = 0;  // Physical cache is a single 2D texture
            
            // CRITICAL: Copy the entire 256x256 tile (including padding), not just actual content
            // This ensures the data layout matches between source and destination
            D3D12_BOX srcBox = {};
            srcBox.left = 0;
            srcBox.top = 0;
            srcBox.right = m_config.tileSize;  // Copy full tile, not just tileActualWidth
            srcBox.bottom = m_config.tileSize;  // Copy full tile, not just tileActualHeight
            srcBox.front = 0;
            srcBox.back = 1;
            
            // Calculate destination position in physical cache
            const uint32_t CACHE_TILES_PER_ROW = 48;  // Must match shader and initialization
            uint32_t pageX = physicalPageIndex % CACHE_TILES_PER_ROW;
            uint32_t pageY = physicalPageIndex / CACHE_TILES_PER_ROW;
            uint32_t dstX = pageX * m_config.tileSize;
            uint32_t dstY = pageY * m_config.tileSize;
            
            // DEBUG: Log first few tiles
            if (totalTilesUploaded < 5) {
                std::cout << "    DEBUG Upload Tile #" << totalTilesUploaded 
                          << ": tex=" << texIdx 
                          << " tile[" << tile.tileX << "," << tile.tileY << "]"
                          << " -> physPage=" << physicalPageIndex
                          << " cachePos[" << dstX << "," << dstY << "]"
                          << " size=" << tileActualWidth << "x" << tileActualHeight << std::endl;
            }
            
            batchCmdList->CopyTextureRegion(&dstLocation, dstX, dstY, 0, &srcLocation, &srcBox);
            
            // Update metadata
            tile.isResident = true;
            tile.physicalPageIndex = physicalPageIndex;
            
            m_physicalPages[physicalPageIndex].isAllocated = true;
            m_physicalPages[physicalPageIndex].virtualTextureIndex = texIdx;
            m_physicalPages[physicalPageIndex].mipLevel = tile.mipLevel;
            m_physicalPages[physicalPageIndex].tileX = tile.tileX;
            m_physicalPages[physicalPageIndex].tileY = tile.tileY;
            
            totalTilesUploaded++;
            currentTileInBatch++;
            
            // Execute batch and wait for GPU to finish before releasing upload buffers
            if (currentTileInBatch >= MAX_TILES_PER_BATCH) {
                std::cout << "  Progress: " << totalTilesUploaded << " tiles uploaded, executing batch..." << std::endl;
                
                // Close and execute command list
                batchCmdList->Close();
                ID3D12CommandList* cmdLists[] = { batchCmdList.Get() };
                commandQueue->ExecuteCommandLists(1, cmdLists);
                
                // Wait for GPU to finish
                commandQueue->Signal(fence.Get(), ++fenceValue);
                if (fence->GetCompletedValue() < fenceValue) {
                    fence->SetEventOnCompletion(fenceValue, fenceEvent);
                    WaitForSingleObject(fenceEvent, INFINITE);
                }
                
                // Now safe to release old upload resources
                m_uploadBuffers.clear();
                // Keep sharedUploadBuffer alive - we'll reuse it for next batch
                texturesInCurrentBatch.clear();
                currentTileInBatch = 0;  // Reset tile counter for next batch
                
                // Reset for next batch
                batchAllocator->Reset();
                batchCmdList->Reset(batchAllocator.Get(), nullptr);
                
                // NOTE: sharedUploadBuffer is persistent across batches - no need to recreate
                
                // Physical cache stays in COPY_DEST state, no need to transition again
            }
        }
        
    }
    
    // Always transition physical cache back to PIXEL_SHADER_RESOURCE at the end
    std::cout << "  Finalizing physical cache state..." << std::endl;
    
    // Check if command list is already closed (from last batch)
    // If uploadBuffers is empty, we need to reset and create a new command list for the final barrier
    if (m_uploadBuffers.empty()) {
        batchAllocator->Reset();
        batchCmdList->Reset(batchAllocator.Get(), nullptr);
    }
    
    // Transition physical cache to NON_PIXEL_SHADER_RESOURCE for DXR compute pipeline
    D3D12_RESOURCE_BARRIER cacheBarrierFinal = {};
    cacheBarrierFinal.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    cacheBarrierFinal.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    cacheBarrierFinal.Transition.pResource = m_physicalCacheTexture.Get();
    cacheBarrierFinal.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    cacheBarrierFinal.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cacheBarrierFinal.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    batchCmdList->ResourceBarrier(1, &cacheBarrierFinal);
    
    // Unmap shared upload buffer BEFORE creating indirection buffer to free memory
    if (sharedUploadBuffer) {
        sharedUploadBuffer->Unmap(0, nullptr);
        std::cout << "  Unmapped shared tile upload buffer" << std::endl;
    }
    
    // Check device status before proceeding
    HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus)) {
        std::cerr << "  ✗ Device lost before indirection upload (HRESULT: 0x" 
                  << std::hex << deviceStatus << std::dec << ")" << std::endl;
        CloseHandle(fenceEvent);
        return false;
    }
    
    // Now upload indirection texture using the same command list and allocator
    // This avoids creating new command resources and prevents resource exhaustion
    std::cout << "  Uploading indirection texture..." << std::endl;
    if (!UploadIndirectionTextureInternal(batchCmdList.Get())) {
        CloseHandle(fenceEvent);
        return false;
    }
    
    batchCmdList->Close();
    ID3D12CommandList* cmdLists[] = { batchCmdList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdLists);
    
    commandQueue->Signal(fence.Get(), ++fenceValue);
    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    
    // sharedUploadBuffer already unmapped before indirection upload
    
    m_uploadBuffers.clear();
    
    // Force GPU to finish all work before releasing command resources
    commandQueue->Signal(fence.Get(), ++fenceValue);
    fence->SetEventOnCompletion(fenceValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
    
    CloseHandle(fenceEvent);
    
    std::cout << "[Virtual Texture] ✓ All tiles uploaded successfully (" << totalTilesUploaded << " total)" << std::endl;
    return true;
}

bool VirtualTextureSystem::UploadIndirectionTextureInternal(ID3D12GraphicsCommandList* cmdList) {
    // Indirection texture: stores physical page index for each virtual tile
    // Size: max texture tiles across all textures
    uint32_t maxTilesX = 0;
    uint32_t maxTilesY = 0;
    
    for (const auto& metadata : m_virtualTextureMetadata) {
        maxTilesX = std::max(maxTilesX, metadata.numTilesX);
        maxTilesY = std::max(maxTilesY, metadata.numTilesY);
    }
    
    if (maxTilesX == 0 || maxTilesY == 0) {
        return false;
    }
    
    std::cout << "[Virtual Texture] Creating indirection texture: " << maxTilesX << "x" << maxTilesY << std::endl;
    std::cout << "  Array slices (textures): " << m_virtualTextureMetadata.size() << std::endl;
    std::cout << "  Format: R32_UINT, Dimensions: " << maxTilesX << "x" << maxTilesY << "x" << m_virtualTextureMetadata.size() << std::endl;
    
    // Release any existing indirection texture first
    if (m_indirectionTexture) {
        std::cout << "  Releasing existing indirection texture..." << std::endl;
        m_indirectionTexture.Reset();
    }
    
    // Check if array size is within D3D12 limits (max 2048 for Texture2DArray)
    if (m_virtualTextureMetadata.size() > 2048) {
        std::cerr << "[Virtual Texture] ✗ Too many textures for Texture2DArray: " << m_virtualTextureMetadata.size() << " (max 2048)" << std::endl;
        return false;
    }
    
    // Create indirection texture (R32_UINT format to store page indices)
    D3D12_RESOURCE_DESC indirectionDesc = {};
    indirectionDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    indirectionDesc.Width = maxTilesX;
    indirectionDesc.Height = maxTilesY;
    indirectionDesc.DepthOrArraySize = static_cast<UINT16>(m_virtualTextureMetadata.size());
    indirectionDesc.MipLevels = 1;
    indirectionDesc.Format = DXGI_FORMAT_R32_UINT;
    indirectionDesc.SampleDesc.Count = 1;
    indirectionDesc.SampleDesc.Quality = 0;
    indirectionDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    indirectionDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = m_device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &indirectionDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_indirectionTexture)
    );
    
    if (FAILED(hr)) {
        std::cerr << "[Virtual Texture] ✗ Failed to create indirection texture (HRESULT: 0x" 
                  << std::hex << hr << std::dec << ")" << std::endl;
        std::cerr << "  Requested: " << maxTilesX << "x" << maxTilesY << "x" << m_virtualTextureMetadata.size() 
                  << " (R32_UINT)" << std::endl;
        size_t estimatedSize = static_cast<size_t>(maxTilesX) * maxTilesY * m_virtualTextureMetadata.size() * 4;
        std::cerr << "  Estimated size: " << (estimatedSize / 1024.0 / 1024.0) << " MB" << std::endl;
        
        // Try to diagnose the issue
        if (hr == E_OUTOFMEMORY || hr == 0x887A0005) {  // DXGI_ERROR_DEVICE_REMOVED
            std::cerr << "  Likely cause: GPU memory exhausted or device lost" << std::endl;
            std::cerr << "  Try: Reduce VT tile batch size or restart application" << std::endl;
        }
        return false;
    }
    
    // Upload indirection data
    // Need to align row pitch to 256 bytes for DX12
    UINT rowPitchBytes = maxTilesX * sizeof(uint32_t);
    UINT alignedRowPitch = (rowPitchBytes + 255) & ~255;
    UINT alignedRowPitchInUints = alignedRowPitch / sizeof(uint32_t);
    
    // Allocate buffer for all slices (each slice is independent)
    // IMPORTANT: Use aligned pitch for buffer allocation (CPU side)
    size_t bytesPerSlice = alignedRowPitch * maxTilesY;
    size_t totalBytes = bytesPerSlice * m_virtualTextureMetadata.size();
    std::vector<uint32_t> indirectionData(totalBytes / sizeof(uint32_t), UINT32_MAX);
    
    // Fill data for each texture (each texture is a separate slice)
    for (size_t texIdx = 0; texIdx < m_virtualTextureMetadata.size(); ++texIdx) {
        const auto& metadata = m_virtualTextureMetadata[texIdx];
        size_t sliceOffsetInUints = texIdx * (bytesPerSlice / sizeof(uint32_t));
        
        for (const auto& tile : metadata.tiles) {
            if (tile.isResident) {
                // Calculate index within this slice using ALIGNED row pitch for upload buffer
                // This matches the PlacedFootprint.RowPitch we'll use in CopyTextureRegion
                size_t idx = sliceOffsetInUints + tile.tileY * alignedRowPitchInUints + tile.tileX;
                indirectionData[idx] = tile.physicalPageIndex;
            }
        }
        
        // DEBUG: Print first texture's indirection data
        if (texIdx == 0) {
            std::cout << "[Virtual Texture] Indirection data for texture 0:" << std::endl;
            std::cout << "  Texture dimensions: " << metadata.width << "x" << metadata.height << std::endl;
            std::cout << "  Tile grid: " << metadata.numTilesX << "x" << metadata.numTilesY << std::endl;
            std::cout << "  Aligned row pitch (uints): " << alignedRowPitchInUints << std::endl;
            for (uint32_t y = 0; y < std::min(3u, metadata.numTilesY); ++y) {
                for (uint32_t x = 0; x < std::min(3u, metadata.numTilesX); ++x) {
                    size_t idx = sliceOffsetInUints + y * alignedRowPitchInUints + x;
                    std::cout << "  Tile[" << x << "," << y << "] idx=" << idx << " page=" << indirectionData[idx];
                    if (indirectionData[idx] == UINT32_MAX) {
                        std::cout << " (NOT RESIDENT)";
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
    
    // Create upload buffer
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indirectionData.size() * sizeof(uint32_t));
    
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    hr = m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );
    
    if (FAILED(hr)) {
        std::cerr << "[Virtual Texture] ✗ Failed to create indirection upload buffer (HRESULT: 0x" 
                  << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }
    
    // Keep upload buffer alive until GPU finishes - add to member vector
    m_uploadBuffers.push_back(uploadBuffer);
    
    if (SUCCEEDED(hr)) {
        // Map upload buffer and copy data
        BYTE* pMappedData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        hr = uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMappedData));
        if (SUCCEEDED(hr)) {
            memcpy(pMappedData, indirectionData.data(), indirectionData.size() * sizeof(uint32_t));
            uploadBuffer->Unmap(0, nullptr);
            
            // Copy each array slice separately
            for (UINT slice = 0; slice < m_virtualTextureMetadata.size(); ++slice) {
                // Calculate aligned row pitch (must be 256-byte aligned for DX12)
                UINT rowPitchBytes = maxTilesX * sizeof(uint32_t);
                UINT alignedRowPitch = (rowPitchBytes + 255) & ~255;  // Round up to 256-byte alignment
                UINT bytesPerSlice = alignedRowPitch * maxTilesY;
                
                D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
                srcLocation.pResource = uploadBuffer.Get();
                srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                srcLocation.PlacedFootprint.Offset = slice * bytesPerSlice;  // Byte offset for each slice
                srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
                srcLocation.PlacedFootprint.Footprint.Width = maxTilesX;
                srcLocation.PlacedFootprint.Footprint.Height = maxTilesY;
                srcLocation.PlacedFootprint.Footprint.Depth = 1;
                srcLocation.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;
                
                D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
                dstLocation.pResource = m_indirectionTexture.Get();
                dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dstLocation.SubresourceIndex = slice;
                
                cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
            }
            
            // Transition to NON_PIXEL_SHADER_RESOURCE for DXR compute pipeline
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_indirectionTexture.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
            );
            cmdList->ResourceBarrier(1, &barrier);
            
            // Command list will be closed and executed by UploadAllTiles
            // No need for independent fence wait
            
            // Now safe to let upload buffer go out of scope
        }
    }
    
    std::cout << "[Virtual Texture] ✓ Indirection texture created" << std::endl;
    return true;
}

bool VirtualTextureSystem::CreateIndirectionTexture(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue) {
    // Indirection texture is now created and uploaded in UploadAllTiles to avoid creating new command resources
    // This function now just returns success for backward compatibility
    std::cout << "[Virtual Texture] Indirection texture already created in UploadAllTiles" << std::endl;
    return true;
}

uint32_t VirtualTextureSystem::CalculateNumTiles(uint32_t dimension, uint32_t tileSize) const {
    return (dimension + tileSize - 1) / tileSize;
}

uint32_t VirtualTextureSystem::AllocatePhysicalPage() {
    if (m_freePhysicalPages.empty()) {
        // Out of physical memory
        size_t allocatedPages = 0;
        for (const auto& page : m_physicalPages) {
            if (page.isAllocated) allocatedPages++;
        }
        std::cerr << "[Virtual Texture] ✗ Out of physical pages: " 
                  << allocatedPages << "/" << m_config.maxPhysicalPages << " allocated" << std::endl;
        return UINT32_MAX;
    }
    
    uint32_t pageIndex = m_freePhysicalPages.front();
    m_freePhysicalPages.pop();
    return pageIndex;
}

void VirtualTextureSystem::FreePhysicalPage(uint32_t pageIndex) {
    if (pageIndex < m_physicalPages.size()) {
        m_physicalPages[pageIndex].isAllocated = false;
        m_freePhysicalPages.push(pageIndex);
    }
}

bool VirtualTextureSystem::MakeTileResident(ID3D12CommandQueue* commandQueue,
                                            uint32_t virtualTextureIndex,
                                            uint32_t mipLevel,
                                            uint32_t tileX,
                                            uint32_t tileY) {
    if (virtualTextureIndex >= m_virtualTextures.size()) {
        return false;
    }
    
    // Allocate physical page
    uint32_t physicalPageIndex = AllocatePhysicalPage();
    if (physicalPageIndex == UINT32_MAX) {
        return false;
    }
    
    auto& metadata = m_virtualTextureMetadata[virtualTextureIndex];
    uint32_t tileIndex = tileY * metadata.numTilesX + tileX;
    
    if (tileIndex >= metadata.tiles.size()) {
        FreePhysicalPage(physicalPageIndex);
        return false;
    }
    
    auto& tile = metadata.tiles[tileIndex];
    
    // Map virtual tile to physical page using UpdateTileMappings
    D3D12_TILED_RESOURCE_COORDINATE tileCoordinate = {};
    tileCoordinate.X = tileX;
    tileCoordinate.Y = tileY;
    tileCoordinate.Z = 0;
    tileCoordinate.Subresource = mipLevel;
    
    D3D12_TILE_REGION_SIZE tileRegionSize = {};
    tileRegionSize.NumTiles = 1;
    tileRegionSize.UseBox = FALSE;
    
    UINT heapRangeStartOffset = physicalPageIndex;
    UINT rangeTileCount = 1;
    
    commandQueue->UpdateTileMappings(
        m_virtualTextures[virtualTextureIndex].Get(),
        1,
        &tileCoordinate,
        &tileRegionSize,
        m_physicalMemoryHeap.Get(),
        1,
        nullptr, // Use default range flags
        &heapRangeStartOffset,
        &rangeTileCount,
        D3D12_TILE_MAPPING_FLAG_NONE
    );
    
    // Update metadata
    tile.isResident = true;
    tile.physicalPageIndex = physicalPageIndex;
    
    m_physicalPages[physicalPageIndex].isAllocated = true;
    m_physicalPages[physicalPageIndex].virtualTextureIndex = virtualTextureIndex;
    m_physicalPages[physicalPageIndex].mipLevel = mipLevel;
    m_physicalPages[physicalPageIndex].tileX = tileX;
    m_physicalPages[physicalPageIndex].tileY = tileY;
    
    return true;
}

void VirtualTextureSystem::EvictTile(ID3D12CommandQueue* commandQueue,
                                    uint32_t virtualTextureIndex,
                                    uint32_t mipLevel,
                                    uint32_t tileX,
                                    uint32_t tileY) {
    if (virtualTextureIndex >= m_virtualTextures.size()) {
        return;
    }
    
    auto& metadata = m_virtualTextureMetadata[virtualTextureIndex];
    uint32_t tileIndex = tileY * metadata.numTilesX + tileX;
    
    if (tileIndex >= metadata.tiles.size()) {
        return;
    }
    
    auto& tile = metadata.tiles[tileIndex];
    
    if (!tile.isResident) {
        return;
    }
    
    // Unmap tile
    D3D12_TILED_RESOURCE_COORDINATE tileCoordinate = {};
    tileCoordinate.X = tileX;
    tileCoordinate.Y = tileY;
    tileCoordinate.Z = 0;
    tileCoordinate.Subresource = mipLevel;
    
    D3D12_TILE_REGION_SIZE tileRegionSize = {};
    tileRegionSize.NumTiles = 1;
    tileRegionSize.UseBox = FALSE;
    
    commandQueue->UpdateTileMappings(
        m_virtualTextures[virtualTextureIndex].Get(),
        1,
        &tileCoordinate,
        &tileRegionSize,
        nullptr, // Null heap unmaps the tile
        0,
        nullptr,
        nullptr,
        nullptr,
        D3D12_TILE_MAPPING_FLAG_NONE
    );
    
    // Free physical page
    FreePhysicalPage(tile.physicalPageIndex);
    
    tile.isResident = false;
    tile.physicalPageIndex = UINT32_MAX;
}

void VirtualTextureSystem::CreateShaderResourceView(ID3D12Device* device,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE indirectionSrvHandle) {
    std::cout << "[VT] CreateShaderResourceView called" << std::endl;
    
    // Create SRV for physical cache texture
    if (m_physicalCacheTexture) {
        std::cout << "[VT]   Creating physical cache SRV at descriptor " << srvHandle.ptr << std::endl;
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        
        device->CreateShaderResourceView(m_physicalCacheTexture.Get(), &srvDesc, srvHandle);
        std::cout << "[VT]   ✓ Physical cache SRV created" << std::endl;
    } else {
        std::cerr << "[VT]   ✗ ERROR: Physical cache texture is NULL!" << std::endl;
    }
    
    // Create SRV for indirection texture
    if (m_indirectionTexture) {
        std::cout << "[VT]   Creating indirection SRV at descriptor " << indirectionSrvHandle.ptr 
                  << " (264 slices)" << std::endl;
        
        D3D12_SHADER_RESOURCE_VIEW_DESC indirectionSrvDesc = {};
        indirectionSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        indirectionSrvDesc.Format = DXGI_FORMAT_R32_UINT;
        indirectionSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        indirectionSrvDesc.Texture2DArray.MostDetailedMip = 0;
        indirectionSrvDesc.Texture2DArray.MipLevels = 1;
        indirectionSrvDesc.Texture2DArray.FirstArraySlice = 0;
        indirectionSrvDesc.Texture2DArray.ArraySize = static_cast<UINT>(m_virtualTextureMetadata.size());
        
        device->CreateShaderResourceView(m_indirectionTexture.Get(), &indirectionSrvDesc, indirectionSrvHandle);
        std::cout << "[VT]   ✓ Indirection SRV created" << std::endl;
    } else {
        std::cerr << "[VT]   ✗ ERROR: Indirection texture is NULL!" << std::endl;
    }
    
    std::cout << "[VT] ✓ All VT SRVs created successfully" << std::endl;
}

VirtualTextureSystem::Statistics VirtualTextureSystem::GetStatistics() const {
    Statistics stats = {};
    stats.numVirtualTextures = static_cast<uint32_t>(m_virtualTextures.size());
    stats.totalPhysicalPages = m_config.maxPhysicalPages;
    stats.usedPhysicalPages = m_config.maxPhysicalPages - static_cast<uint32_t>(m_freePhysicalPages.size());
    
    stats.physicalMemoryMB = (static_cast<uint64_t>(m_config.maxPhysicalPages) * 65536) / (1024 * 1024);
    
    uint64_t totalVirtualMemBytes = 0;
    for (const auto& metadata : m_virtualTextureMetadata) {
        totalVirtualMemBytes += static_cast<uint64_t>(metadata.width) * metadata.height * 4; // RGBA
    }
    stats.totalVirtualMemoryMB = totalVirtualMemBytes / (1024 * 1024);
    
    return stats;
}

void VirtualTextureSystem::ProcessFeedback(const void* feedbackData, size_t dataSize) {
    // TODO: Implement feedback-driven streaming
    // This would analyze which tiles are actually being rendered
    // and prioritize loading those tiles
}

VirtualTextureSystem::VirtualTextureInfo VirtualTextureSystem::GetTextureInfo(uint32_t virtualTextureIndex) const {
    VirtualTextureInfo info = {};
    
    if (virtualTextureIndex < m_virtualTextureMetadata.size()) {
        const auto& metadata = m_virtualTextureMetadata[virtualTextureIndex];
        info.tileSize = m_config.tileSize;
        info.numTilesX = metadata.numTilesX;
        info.numTilesY = metadata.numTilesY;
        info.physicalPageIndex = 0; // TODO: Return actual page mapping
    }
    
    return info;
}

} // namespace ACG
