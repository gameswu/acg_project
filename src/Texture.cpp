#include "Texture.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
    std::cout << "Loading texture: " << filename << std::endl;
    
    int width, height, channels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: " << filename << std::endl;
        std::cerr << "stbi_failure_reason: " << stbi_failure_reason() << std::endl;
        return false;
    }
    
    Create(width, height, channels, data);
    stbi_image_free(data);
    
    std::cout << "Loaded texture: " << width << "x" << height << " (" << channels << " channels)" << std::endl;
    return true;
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
    if (m_mipLevels.empty()) {
        return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    
    int level = std::clamp(static_cast<int>(mipLevel), 0, static_cast<int>(m_mipLevels.size()) - 1);
    const MipLevel& mip = m_mipLevels[level];
    
    glm::vec2 uv = ApplyWrap(u, v);
    
    // Convert UV to pixel coordinates
    float px = uv.x * mip.width - 0.5f;
    float py = uv.y * mip.height - 0.5f;
    
    int x0 = static_cast<int>(std::floor(px));
    int y0 = static_cast<int>(std::floor(py));
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    float fx = px - x0;
    float fy = py - y0;
    
    // Get four corner pixels
    glm::vec4 c00 = GetPixel(level, x0, y0);
    glm::vec4 c10 = GetPixel(level, x1, y0);
    glm::vec4 c01 = GetPixel(level, x0, y1);
    glm::vec4 c11 = GetPixel(level, x1, y1);
    
    // Bilinear interpolation
    glm::vec4 c0 = glm::mix(c00, c10, fx);
    glm::vec4 c1 = glm::mix(c01, c11, fx);
    return glm::mix(c0, c1, fy);
}

glm::vec4 Texture::SampleTrilinear(float u, float v, float mipLevel) const {
    if (m_mipLevels.empty()) {
        return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    
    mipLevel = std::clamp(mipLevel, 0.0f, static_cast<float>(m_mipLevels.size() - 1));
    
    int level0 = static_cast<int>(std::floor(mipLevel));
    int level1 = std::min(level0 + 1, static_cast<int>(m_mipLevels.size()) - 1);
    float frac = mipLevel - level0;
    
    // Sample both mip levels with bilinear filtering
    glm::vec4 sample0 = SampleBilinear(u, v, static_cast<float>(level0));
    glm::vec4 sample1 = SampleBilinear(u, v, static_cast<float>(level1));
    
    // Interpolate between mip levels
    return glm::mix(sample0, sample1, frac);
}

void Texture::GenerateMipmaps() {
    if (m_mipLevels.empty()) {
        return;
    }
    
    int levelCount = 1 + static_cast<int>(std::floor(std::log2(std::max(m_width, m_height))));
    
    for (int level = 1; level < levelCount; ++level) {
        const MipLevel& prevLevel = m_mipLevels[level - 1];
        int newWidth = std::max(1, prevLevel.width / 2);
        int newHeight = std::max(1, prevLevel.height / 2);
        
        MipLevel newLevel;
        newLevel.width = newWidth;
        newLevel.height = newHeight;
        newLevel.data.resize(newWidth * newHeight * m_channels);
        
        // Box filter downsampling
        for (int y = 0; y < newHeight; ++y) {
            for (int x = 0; x < newWidth; ++x) {
                int sx = x * 2;
                int sy = y * 2;
                
                for (int c = 0; c < m_channels; ++c) {
                    int sum = 0;
                    int count = 0;
                    
                    for (int dy = 0; dy < 2 && (sy + dy) < prevLevel.height; ++dy) {
                        for (int dx = 0; dx < 2 && (sx + dx) < prevLevel.width; ++dx) {
                            int srcIdx = ((sy + dy) * prevLevel.width + (sx + dx)) * m_channels + c;
                            sum += prevLevel.data[srcIdx];
                            count++;
                        }
                    }
                    
                    int dstIdx = (y * newWidth + x) * m_channels + c;
                    newLevel.data[dstIdx] = static_cast<unsigned char>(sum / count);
                }
            }
        }
        
        m_mipLevels.push_back(newLevel);
    }
    
    std::cout << "Generated " << m_mipLevels.size() << " mipmap levels" << std::endl;
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
            u = u - std::floor(u);
            v = v - std::floor(v);
            if (static_cast<int>(std::floor(u)) & 1) u = 1.0f - u;
            if (static_cast<int>(std::floor(v)) & 1) v = 1.0f - v;
            break;
    }
    return glm::vec2(u, v);
}

glm::vec4 Texture::SampleMipLevel(int level, float u, float v) const {
    if (m_mipLevels.empty() || level >= m_mipLevels.size()) {
        return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta for missing texture
    }
    
    glm::vec2 uv = ApplyWrap(u, v);
    const MipLevel& mip = m_mipLevels[level];
    
    int x = static_cast<int>(uv.x * mip.width) % mip.width;
    int y = static_cast<int>(uv.y * mip.height) % mip.height;
    
    return GetPixel(level, x, y);
}

glm::vec4 Texture::GetPixel(int level, int x, int y) const {
    if (level < 0 || level >= m_mipLevels.size()) {
        return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    
    const MipLevel& mip = m_mipLevels[level];
    
    // Clamp coordinates
    x = std::clamp(x, 0, mip.width - 1);
    y = std::clamp(y, 0, mip.height - 1);
    
    int idx = (y * mip.width + x) * m_channels;
    
    glm::vec4 color(0.0f);
    for (int c = 0; c < m_channels && c < 4; ++c) {
        color[c] = mip.data[idx + c] / 255.0f;
    }
    
    // Set alpha to 1.0 if no alpha channel
    if (m_channels < 4) {
        color.a = 1.0f;
    }
    
    return color;
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
