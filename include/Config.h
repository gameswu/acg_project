#pragma once

#include <string>
#include <glm/glm.hpp>

namespace ACG {

struct PersistentConfig {
    // Scene paths
    std::string lastModelPath;
    std::string lastEnvMapPath;
    
    // Camera settings
    glm::vec3 cameraPosition = glm::vec3(0.0f, 1.0f, 5.0f);
    glm::vec3 cameraDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    float cameraFOV = 60.0f;
    bool enableDepthOfField = false;
    float focusDistance = 5.0f;
    float aperture = 0.1f;
    
    // Render settings
    int samplesPerPixel = 100;
    int maxBounces = 5;
    float envLightIntensity = 0.5f;
    int vtTileBatchSize = 50;  // Virtual Texture tiles per batch
    int renderBatchSize = 5;   // Samples per GPU execution batch
    
    // Sun settings
    float sunIntensity = 5.0f;
    float sunAzimuth = 45.0f;
    float sunElevation = 30.0f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);
    
    // Output settings
    int outputWidth = 1920;
    int outputHeight = 1080;
    std::string lastOutputPath;
    
    // Load configuration from file
    bool Load(const std::string& filename);
    
    // Save configuration to file
    bool Save(const std::string& filename) const;
};

} // namespace ACG
