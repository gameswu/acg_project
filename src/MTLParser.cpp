#include "MTLParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace ACG {

std::vector<MTLParser::MTLMaterial> MTLParser::Parse(const std::string& filepath) {
    std::vector<MTLMaterial> materials;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "MTLParser: Failed to open file: " << filepath << std::endl;
        return materials;
    }
    
    std::cout << "MTLParser: Parsing " << filepath << std::endl;
    
    MTLMaterial* currentMaterial = nullptr;
    std::string line;
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        line = Trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        auto tokens = Split(line);
        if (tokens.empty()) continue;
        
        const std::string& keyword = tokens[0];
        
        // New material definition
        if (keyword == "newmtl") {
            if (tokens.size() < 2) {
                std::cerr << "MTLParser: Line " << lineNumber << ": newmtl requires a name" << std::endl;
                continue;
            }
            
            // Save previous material and start new one
            materials.emplace_back();
            currentMaterial = &materials.back();
            currentMaterial->name = tokens[1];
            
            std::cout << "  [MTL] newmtl \"" << currentMaterial->name << "\"" << std::endl;
        }
        else if (currentMaterial) {
            // Parse material properties
            if (keyword == "Ka" && tokens.size() >= 2) {
                currentMaterial->Ka = ParseColor(tokens[1], 
                    tokens.size() > 2 ? tokens[2] : "", 
                    tokens.size() > 3 ? tokens[3] : "");
                currentMaterial->hasKa = true;
            }
            else if (keyword == "Kd" && tokens.size() >= 2) {
                currentMaterial->Kd = ParseColor(tokens[1], 
                    tokens.size() > 2 ? tokens[2] : "", 
                    tokens.size() > 3 ? tokens[3] : "");
            }
            else if (keyword == "Ks" && tokens.size() >= 2) {
                currentMaterial->Ks = ParseColor(tokens[1], 
                    tokens.size() > 2 ? tokens[2] : "", 
                    tokens.size() > 3 ? tokens[3] : "");
                currentMaterial->hasKs = true;
            }
            else if (keyword == "Ke" && tokens.size() >= 2) {
                currentMaterial->Ke = ParseColor(tokens[1], 
                    tokens.size() > 2 ? tokens[2] : "", 
                    tokens.size() > 3 ? tokens[3] : "");
            }
            else if (keyword == "Tf" && tokens.size() >= 2) {
                currentMaterial->Tf = ParseColor(tokens[1], 
                    tokens.size() > 2 ? tokens[2] : "", 
                    tokens.size() > 3 ? tokens[3] : "");
            }
            else if (keyword == "Ns" && tokens.size() >= 2) {
                currentMaterial->Ns = std::stof(tokens[1]);
            }
            else if (keyword == "d" && tokens.size() >= 2) {
                // Handle "d -halo factor" (ignore -halo for now)
                if (tokens[1] == "-halo" && tokens.size() >= 3) {
                    currentMaterial->d = std::stof(tokens[2]);
                } else {
                    currentMaterial->d = std::stof(tokens[1]);
                }
            }
            else if (keyword == "Ni" && tokens.size() >= 2) {
                currentMaterial->Ni = std::stof(tokens[1]);
                // MTL Spec: Valid range is 0.001-10
                // 1.0 = air (no bending), 1.5 = glass, 1.33 = water, 2.42 = diamond
                if (currentMaterial->Ni < 0.001f) currentMaterial->Ni = 0.001f;
                if (currentMaterial->Ni > 10.0f) currentMaterial->Ni = 10.0f;
                currentMaterial->hasNi = true;
            }
            else if (keyword == "illum" && tokens.size() >= 2) {
                currentMaterial->illum = std::stoi(tokens[1]);
                // MTL Spec: Valid range is 0-10
                if (currentMaterial->illum < 0) currentMaterial->illum = 0;
                if (currentMaterial->illum > 10) currentMaterial->illum = 10;
            }
            else if (keyword == "sharpness" && tokens.size() >= 2) {
                currentMaterial->sharpness = std::stof(tokens[1]);
                // MTL Spec: Valid range is 0-1000, default 60
                if (currentMaterial->sharpness < 0.0f) currentMaterial->sharpness = 0.0f;
                if (currentMaterial->sharpness > 1000.0f) currentMaterial->sharpness = 1000.0f;
                currentMaterial->hasSharpness = true;
            }
            // Texture maps (MTL Spec section: Material texture map)
            else if (keyword == "map_Ka" && tokens.size() >= 2) {
                currentMaterial->map_Ka = tokens[tokens.size() - 1]; // Last token is filename
            }
            else if (keyword == "map_Kd" && tokens.size() >= 2) {
                currentMaterial->map_Kd = tokens[tokens.size() - 1];
            }
            else if (keyword == "map_Ks" && tokens.size() >= 2) {
                currentMaterial->map_Ks = tokens[tokens.size() - 1];
            }
            else if (keyword == "map_Ns" && tokens.size() >= 2) {
                currentMaterial->map_Ns = tokens[tokens.size() - 1];
            }
            else if (keyword == "map_d" && tokens.size() >= 2) {
                currentMaterial->map_d = tokens[tokens.size() - 1];
            }
            else if ((keyword == "map_bump" || keyword == "bump") && tokens.size() >= 2) {
                currentMaterial->map_bump = tokens[tokens.size() - 1];
            }
            else if (keyword == "disp" && tokens.size() >= 2) {
                currentMaterial->map_disp = tokens[tokens.size() - 1];
            }
            else if (keyword == "decal" && tokens.size() >= 2) {
                currentMaterial->map_decal = tokens[tokens.size() - 1];
            }
            else if (keyword == "refl" && tokens.size() >= 2) {
                currentMaterial->map_refl = tokens[tokens.size() - 1];
            }
        }
    }
    
    file.close();
    std::cout << "MTLParser: Parsed " << materials.size() << " materials" << std::endl;
    return materials;
}

std::shared_ptr<Material> MTLParser::ConvertToMaterial(
    const MTLMaterial& mtl,
    const std::string& mtlDirectory) {
    std::shared_ptr<Material> mat;
    
    // Calculate helper values
    float emissionIntensity = std::max({mtl.Ke.r, mtl.Ke.g, mtl.Ke.b});
    float specularIntensity = (mtl.Ks.r + mtl.Ks.g + mtl.Ks.b) / 3.0f;
    
    // ========== MTL SPECIFICATION v4.2 COMPLIANT MATERIAL CLASSIFICATION ==========
    // Reference: Wavefront MTL Format Specification v4.2, October 1995, page 5-7
    //
    // ILLUMINATION MODELS (illum parameter):
    //
    // illum 0:  Color on, Ambient off
    //           Constant color (Kd), no lighting calculations
    //
    // illum 1:  Color on, Ambient on  
    //           Diffuse illumination using Lambertian shading
    //           color = Ka*Ia + Kd*SUM(N·L*I)
    //
    // illum 2:  Highlight on
    //           Diffuse + Specular (Lambertian + Blinn-Phong)
    //           color = Ka*Ia + Kd*SUM(N·L*I) + Ks*SUM((H·H)^Ns*I)
    //           [MOST COMMON for standard materials]
    //
    // illum 3:  Reflection on, Ray trace on
    //           Reflective material (metal/mirror) with ray tracing
    //           Includes diffuse + specular + reflection term (Ir)
    //           CRITICAL: Requires Ks > 0 for actual reflection
    //
    // illum 4:  Transparency: Glass on, Reflection: Ray trace on
    //           Simulates glass with highlights and reflections
    //           Uses dissolve (d) for transparency
    //
    // illum 5:  Reflection: Fresnel on, Ray trace on
    //           Perfect mirror with Fresnel reflection
    //           Reflectivity increases at grazing angles
    //
    // illum 6:  Transparency: Refraction on, Reflection: Fresnel off, Ray trace on
    //           Refractive material (glass) using optical density (Ni)
    //           Light bends according to IOR, filtered by Tf
    //
    // illum 7:  Transparency: Refraction on, Reflection: Fresnel on, Ray trace on
    //           Realistic glass combining refraction and Fresnel reflection
    //
    // illum 8:  Reflection on, Ray trace off
    //           Reflection using only reflection map (no ray tracing)
    //
    // illum 9:  Transparency: Glass on, Reflection: Ray trace off
    //           Transparent material using only reflection map
    //
    // illum 10: Casts shadows onto invisible surfaces
    //           Shadow matte for compositing CG onto live action
    //
    // ========== CLASSIFICATION PRIORITY ==========
    
    // Priority 1: Emissive materials (Ke > 0, any illum)
    if (emissionIntensity > 0.01f) {
        mat = std::make_shared<Material>();
        mat->SetType(MaterialType::Emissive);
        mat->SetAlbedo(mtl.Kd);
        mat->SetEmission(mtl.Ke);
        mat->SetSpecular(mtl.Ks);
        mat->SetIllum(mtl.illum);
        
        std::cout << "    → Emissive: Ke=(" << mtl.Ke.r << "," << mtl.Ke.g << "," << mtl.Ke.b << ")" << std::endl;
    }
    else if (mtl.illum == 4 || mtl.illum == 6 || mtl.illum == 7 || mtl.illum == 9) {
        // Check if this is truly a transmissive material
        // Blender exports illum=4 for all raytraced materials, but with Tf=1.0 and Ni=1.0 for opaque ones
        // True glass: Tf < 1.0 (absorbs some light) OR Ni > 1.05 (has refraction)
        float tfAvg = (mtl.Tf.r + mtl.Tf.g + mtl.Tf.b) / 3.0f;
        bool isTransmissive = (tfAvg < 0.99f) || (mtl.Ni > 1.05f);
        
        if (isTransmissive) {
            glm::vec3 transmissionColor = mtl.Tf;
            
            mat = std::make_shared<TransmissiveMaterial>(transmissionColor, mtl.Ni);
            mat->SetAlbedo(transmissionColor);
            mat->SetTransmissionFilter(mtl.Tf);
            mat->SetSpecular(mtl.Ks);
            mat->SetIOR(mtl.Ni);
            mat->SetTransmission(1.0f - mtl.d);
            mat->SetRoughness(mtl.Ns > 0.0f ? std::sqrt(2.0f / (mtl.Ns + 2.0f)) : 0.05f);
            mat->SetIllum(mtl.illum);
        } else {
            // Blender's illum=4 with Tf=1.0 and Ni=1.0 -> treat as diffuse/specular
            mat = std::make_shared<DiffuseMaterial>(mtl.Kd);
            mat->SetAlbedo(mtl.Kd);
            mat->SetSpecular(mtl.Ks);
            mat->SetRoughness(mtl.Ns > 0.0f ? std::sqrt(2.0f / (mtl.Ns + 2.0f)) : 0.05f);
            mat->SetMetallic(0.0f);
            mat->SetIllum(mtl.illum);
        }
    }
    // Priority 3: Mirror/Reflective materials (illum 3, 5, 8 AND Ks > 0)
    // MTL SPEC CRITICAL RULE: illum 3 with Ks=0 is NOT a mirror!
    // Reflection requires BOTH:
    //   1. illum model with reflection enabled (3, 5, 8)
    //   2. Non-zero specular reflectivity (Ks > 0)
    // Common mistake: Treating illum=3 as mirror without checking Ks
    else if ((mtl.illum == 3 || mtl.illum == 5 || mtl.illum == 8) && specularIntensity > 0.01f) {
        mat = std::make_shared<SpecularMaterial>(mtl.Ks, 0.0f);
        mat->SetAlbedo(mtl.Kd);      // For colored mirrors
        mat->SetSpecular(mtl.Ks);    // Mirror reflectance color
        mat->SetRoughness(0.0f);     // Perfect mirror reflection
        mat->SetMetallic(1.0f);      // Full metallic for mirror
        mat->SetIllum(mtl.illum);
        
        std::cout << "    → Mirror/Reflective: illum=" << mtl.illum 
                  << ", Ks=(" << mtl.Ks.r << "," << mtl.Ks.g << "," << mtl.Ks.b << ")" << std::endl;
    }
    // Priority 4: Standard Diffuse/Specular materials
    // Covers: illum 0, 1, 2, or illum 3/5/8 with Ks=0
    else {
        mat = std::make_shared<DiffuseMaterial>(mtl.Kd);
        mat->SetAlbedo(mtl.Kd);
        mat->SetSpecular(mtl.Ks);
        // Convert Phong exponent (Ns) to PBR roughness
        // Disney formula: roughness = sqrt(2/(Ns+2))
        float roughness = (mtl.Ns > 0.0f) ? std::sqrt(2.0f / (mtl.Ns + 2.0f)) : 0.5f;
        mat->SetRoughness(roughness);
        mat->SetMetallic(0.0f);      // Diffuse materials are non-metallic
        mat->SetIllum(mtl.illum);
        
        std::cout << "    → Diffuse";
        if (mtl.illum == 0) std::cout << " (Flat Color)";
        else if (mtl.illum == 1) std::cout << " (Lambertian)";
        else if (mtl.illum == 2) std::cout << " (Blinn-Phong)";
        else if (mtl.illum == 3 || mtl.illum == 5 || mtl.illum == 8) 
            std::cout << " (illum=" << mtl.illum << " but Ks=0, treated as diffuse per MTL spec)";
        else std::cout << " (illum=" << mtl.illum << ")";
        
        std::cout << ": Kd=(" << mtl.Kd.r << "," << mtl.Kd.g << "," << mtl.Kd.b << ")";
        if (specularIntensity > 0.01f) {
            std::cout << ", Ks=(" << mtl.Ks.r << "," << mtl.Ks.g << "," << mtl.Ks.b << ")";
            std::cout << ", Ns=" << mtl.Ns;
        }
        std::cout << std::endl;
    }
    
    // Load textures if specified
    std::cout << "    → Checking texture: map_Kd=" << (mtl.map_Kd.empty() ? "empty" : mtl.map_Kd) 
              << ", mtlDirectory=" << (mtlDirectory.empty() ? "empty" : mtlDirectory) << std::endl;
    
    if (!mtl.map_Kd.empty() && !mtlDirectory.empty()) {
        std::string texturePath = mtlDirectory + "\\" + mtl.map_Kd;
        auto texture = std::make_shared<Texture>();
        std::cout << "    → Loading texture: " << texturePath << std::endl;
        if (texture->LoadFromFile(texturePath)) {
            mat->SetAlbedoTexture(texture);
            std::cout << "       ✓ Texture loaded: " << texture->GetWidth() << "x" << texture->GetHeight() << std::endl;
        } else {
            std::cout << "       ✗ Failed to load texture" << std::endl;
        }
    }
    
    return mat;
}

glm::vec3 MTLParser::ParseColor(const std::string& r, const std::string& g, const std::string& b) {
    float red = std::stof(r);
    
    // If only one component, use it for all channels (grayscale)
    if (g.empty() || b.empty()) {
        return glm::vec3(red, red, red);
    }
    
    return glm::vec3(red, std::stof(g), std::stof(b));
}

std::string MTLParser::Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> MTLParser::Split(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

} // namespace ACG
