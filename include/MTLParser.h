#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "Material.h"

namespace ACG {

/**
 * @brief MTL (Wavefront Material) file parser
 * 
 * Implements strict MTL specification v4.2 (October 1995)
 * Parses .mtl files and creates Material objects with correct properties.
 * 
 * This parser avoids Assimp bugs, particularly:
 * - Incorrect illum value mapping (illum 2 -> 3)
 * - Missing or incorrect Ks/Ka/Kd values
 * - IOR always returning success even when undefined
 */
class MTLParser {
public:
    /**
     * @brief MTL material definition (intermediate representation)
     * 
     * Fully compliant with MTL specification v4.2 (October 1995)
     * All properties match the official Wavefront specification
     */
    struct MTLMaterial {
        std::string name;
        
        // MTL color properties (RGB reflectivity, range 0.0-1.0)
        // Note: Spec also supports "spectral file.rfl factor" and "xyz x y z" formats
        glm::vec3 Ka{0.0f, 0.0f, 0.0f};   // Ambient reflectivity (default: black)
        glm::vec3 Kd{0.8f, 0.8f, 0.8f};   // Diffuse reflectivity (default: gray)
        glm::vec3 Ks{0.0f, 0.0f, 0.0f};   // Specular reflectivity (default: black)
        glm::vec3 Ke{0.0f, 0.0f, 0.0f};   // Emissive color (extension, not in spec)
        glm::vec3 Tf{1.0f, 1.0f, 1.0f};   // Transmission filter (default: white = full transmission)
        
        // MTL scalar properties
        float Ns = 0.0f;                   // Specular exponent (0-1000, focus of highlight)
        float d = 1.0f;                    // Dissolve (0.0=transparent, 1.0=opaque)
        float Ni = 1.0f;                   // Optical density/IOR (1.0=air, 1.5=glass, range 0.001-10)
        int illum = 2;                     // Illumination model (0-10)
        float sharpness = 60.0f;           // Reflection map sharpness (0-1000, default 60)
        
        // Texture maps (basic filenames, options parsing not implemented)
        std::string map_Ka;                // Ambient texture map
        std::string map_Kd;                // Diffuse texture map
        std::string map_Ks;                // Specular texture map
        std::string map_Ns;                // Specular exponent texture (scalar)
        std::string map_d;                 // Dissolve/alpha texture (scalar)
        std::string map_bump;              // Bump/normal map
        std::string map_disp;              // Displacement map (surface roughness)
        std::string map_decal;             // Decal texture (selective color replacement)
        std::string map_refl;              // Reflection map (sphere or cube)
        
        // Parsing flags (detect if property was explicitly set)
        bool hasKa = false;
        bool hasKs = false;
        bool hasNi = false;
        bool hasD = false;
        bool hasSharpness = false;
    };
    
    MTLParser() = default;
    ~MTLParser() = default;
    
    /**
     * @brief Parse MTL file and return material definitions
     * @param filepath Path to .mtl file
     * @return Vector of parsed materials
     */
    std::vector<MTLMaterial> Parse(const std::string& filepath);
    
    /**
     * @brief Convert MTL material to engine Material object
     * @param mtl MTL material definition
     * @param mtlDirectory Directory of the MTL file for loading textures
     * @return Shared pointer to Material object
     */
    static std::shared_ptr<Material> ConvertToMaterial(
        const MTLMaterial& mtl,
        const std::string& mtlDirectory = "");
    
private:
    /**
     * @brief Parse a single line from MTL file
     */
    void ParseLine(const std::string& line, MTLMaterial* currentMaterial);
    
    /**
     * @brief Parse RGB color values (supports 1 or 3 components)
     */
    glm::vec3 ParseColor(const std::string& r, const std::string& g = "", const std::string& b = "");
    
    /**
     * @brief Trim whitespace from string
     */
    std::string Trim(const std::string& str);
    
    /**
     * @brief Split string by whitespace
     */
    std::vector<std::string> Split(const std::string& str);
};

} // namespace ACG
