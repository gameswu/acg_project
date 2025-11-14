#include "Texture.h"
#include <iostream>
#include <cmath>
#include <algorithm>

// For image loading, you would use stb_image.h:
// #define STB_IMAGE_IMPLEMENTATION
// #include <stb_image.h>

namespace ACG {

Texture::Texture()
    : m_width(0)
    , m_height(0)
    , m_channels(0)
    , m_type(TextureType::Color)
    , m_filter(TextureFilter::Bilinear)
    , m_wrap(TextureWrap::Repeat)
{
}

Texture::~Texture() {
}

bool Texture::LoadFromFile(const std::string& filename) {
    // TODO: Use stb_image to load texture
    // int width, height, channels;
    // unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    // if (data) {
    //     Create(width, height, channels, data);
    //     stbi_image_free(data);
    //     return true;
    // }
    
    std::cout << "Loading texture: " << filename << std::endl;
    return false;
}

void Texture::Create(int width, int height, int channels, const unsigned char* data) {
    m_width = width;
    m_height = height;
    m_channels = channels;
    
    // Store base mip level
    MipLevel level0;
    level0.width = width;
    level0.height = height;
    level0.data.resize(width * height * channels);
    if (data) {
        std::copy(data, data + width * height * channels, level0.data.begin());
    }
    
    m_mipLevels.clear();
    m_mipLevels.push_back(level0);
}

glm::vec4 Texture::Sample(float u, float v, float mipLevel) const {
    // TODO: Sample based on filter mode
    switch (m_filter) {
        case TextureFilter::Nearest:
            return SampleMipLevel(0, u, v);
        case TextureFilter::Bilinear:
            return SampleBilinear(u, v, mipLevel);
        case TextureFilter::Trilinear:
            return SampleTrilinear(u, v, mipLevel);
        default:
            return glm::vec4(1.0f);
    }
}

glm::vec4 Texture::SampleBilinear(float u, float v, float mipLevel) const {
    // TODO: Implement bilinear filtering
    return SampleMipLevel(0, u, v);
}

glm::vec4 Texture::SampleTrilinear(float u, float v, float mipLevel) const {
    // TODO: Implement trilinear filtering (lerp between mip levels)
    return SampleMipLevel(0, u, v);
}

void Texture::GenerateMipmaps() {
    // TODO: Generate mipmap chain using box filter
    int levelCount = 1 + static_cast<int>(std::floor(std::log2(std::max(m_width, m_height))));
    // Generate each mip level...
}

void Texture::GenerateAdaptiveMipmaps() {
    // TODO: Implement adaptive mipmap generation algorithm
    // This is a 2pts advanced feature
}

glm::vec2 Texture::ApplyWrap(float u, float v) const {
    // TODO: Apply texture wrapping mode
    switch (m_wrap) {
        case TextureWrap::Repeat:
            u = u - std::floor(u);
            v = v - std::floor(v);
            break;
        case TextureWrap::Clamp:
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            break;
        case TextureWrap::Mirror:
            // TODO: Mirror wrapping
            break;
    }
    return glm::vec2(u, v);
}

glm::vec4 Texture::SampleMipLevel(int level, float u, float v) const {
    if (m_mipLevels.empty() || level >= m_mipLevels.size()) {
        return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta for missing texture
    }
    
    // TODO: Sample pixel from mip level
    return glm::vec4(1.0f);
}

glm::vec4 Texture::GetPixel(int level, int x, int y) const {
    // TODO: Get pixel value from texture data
    return glm::vec4(1.0f);
}

// TextureManager implementation

TextureManager& TextureManager::Instance() {
    static TextureManager instance;
    return instance;
}

std::shared_ptr<Texture> TextureManager::Load(const std::string& filename) {
    // Check if already loaded
    auto it = m_textures.find(filename);
    if (it != m_textures.end()) {
        return it->second;
    }
    
    // Load new texture
    auto texture = std::make_shared<Texture>();
    if (texture->LoadFromFile(filename)) {
        m_textures[filename] = texture;
        return texture;
    }
    
    return nullptr;
}

void TextureManager::Clear() {
    m_textures.clear();
}

} // namespace ACG
