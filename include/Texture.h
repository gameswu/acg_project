#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>

namespace ACG {

enum class TextureType {
    Color,          // RGB color texture
    Normal,         // Normal map
    Roughness,      // Roughness/smoothness map
    Metallic,       // Metallic map
    Height,         // Height/displacement map
    AO,             // Ambient occlusion
    Emissive        // Emission map
};

enum class TextureFilter {
    Nearest,
    Bilinear,
    Trilinear,
    Anisotropic
};

enum class TextureWrap {
    Repeat,
    Clamp,
    Mirror
};

/**
 * @brief Texture with mipmap support
 * 
 * Supports:
 * - Basic color textures
 * - Normal maps
 * - PBR texture maps (roughness, metallic, etc.)
 * - Adaptive mipmapping
 */
class Texture {
public:
    Texture();
    ~Texture();

    // Load texture from file
    bool LoadFromFile(const std::string& filename);
    
    // Create texture from data
    void Create(int width, int height, int channels, const unsigned char* data);
    
    // Sample texture
    glm::vec4 Sample(float u, float v, float mipLevel = 0.0f) const;
    glm::vec4 SampleBilinear(float u, float v, float mipLevel = 0.0f) const;
    glm::vec4 SampleTrilinear(float u, float v, float mipLevel = 0.0f) const;
    
    // Generate mipmaps
    void GenerateMipmaps();
    void GenerateAdaptiveMipmaps();  // Advanced: adaptive mipmap algorithm
    
    // Getters
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    int GetChannels() const { return m_channels; }
    int GetMipLevels() const { return static_cast<int>(m_mipLevels.size()); }
    const unsigned char* GetRawData() const { return m_mipLevels.empty() ? nullptr : m_mipLevels[0].data.data(); }
    
    // Settings
    void SetFilter(TextureFilter filter) { m_filter = filter; }
    void SetWrap(TextureWrap wrap) { m_wrap = wrap; }
    void SetType(TextureType type) { m_type = type; }

private:
    struct MipLevel {
        int width;
        int height;
        std::vector<unsigned char> data;
    };
    
    int m_width;
    int m_height;
    int m_channels;
    
    std::vector<MipLevel> m_mipLevels;
    
    TextureType m_type;
    TextureFilter m_filter;
    TextureWrap m_wrap;
    
    // Helper methods
    glm::vec2 ApplyWrap(float u, float v) const;
    glm::vec4 SampleMipLevel(int level, float u, float v) const;
    glm::vec4 GetPixel(int level, int x, int y) const;
};

/**
 * @brief Texture manager for loading and caching textures
 */
class TextureManager {
public:
    static TextureManager& Instance();
    
    std::shared_ptr<Texture> Load(const std::string& filename);
    void Clear();

private:
    TextureManager() = default;
    std::map<std::string, std::shared_ptr<Texture>> m_textures;
};

} // namespace ACG
