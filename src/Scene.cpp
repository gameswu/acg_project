#include "Scene.h"
#include <iostream>
#include <limits>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

namespace ACG {

Scene::Scene() 
    : m_name("Untitled Scene")
    , m_bboxMin(std::numeric_limits<float>::max())
    , m_bboxMax(std::numeric_limits<float>::lowest())
{
}

Scene::~Scene() {
}

void Scene::AddMesh(std::shared_ptr<Mesh> mesh) {
    m_meshes.push_back(mesh);
}

void Scene::AddMaterial(std::shared_ptr<Material> material) {
    m_materials.push_back(material);
}

void Scene::AddLight(std::shared_ptr<Light> light) {
    m_lights.push_back(light);
}

bool Scene::LoadFromFile(const std::string& filename) {
    std::cout << "Loading scene from: " << filename << std::endl;
    
    // Extract directory from filename for texture loading and scene name
    std::string directory;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        directory = filename.substr(0, lastSlash + 1);
        
        // 从文件路径提取场景名称
        size_t lastDot = filename.find_last_of(".");
        if (lastDot != std::string::npos && lastDot > lastSlash) {
            m_name = filename.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else {
            m_name = "Unnamed Scene";
        }
    } else {
        m_name = "Unnamed Scene";
    }
    
    Assimp::Importer importer;
    const aiScene* ascene = importer.ReadFile(
        filename,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs);
    if (!ascene || ascene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !ascene->mRootNode) {
        std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
        return false;
    }

    m_meshes.clear();
    m_materials.clear();
    
    // Load materials from aiScene
    for (unsigned int i = 0; i < ascene->mNumMaterials; ++i) {
        aiMaterial* aiMat = ascene->mMaterials[i];
        
        // ========== MTL SPECIFICATION COMPLIANT MATERIAL LOADING ==========
        // Reference: Wavefront MTL Format Specification v4.2, October 1995
        
        // Get material name
        aiString name;
        aiMat->Get(AI_MATKEY_NAME, name);
        
        // Read all MTL color properties (specification section 5)
        // Ka: Ambient reflectivity (RGB 0.0-1.0)
        aiColor3D Ka(0.0f, 0.0f, 0.0f);
        aiMat->Get(AI_MATKEY_COLOR_AMBIENT, Ka);
        glm::vec3 ambient(Ka.r, Ka.g, Ka.b);
        
        // Kd: Diffuse reflectivity (RGB 0.0-1.0)
        aiColor3D Kd(0.8f, 0.8f, 0.8f);
        aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, Kd);
        glm::vec3 diffuse(Kd.r, Kd.g, Kd.b);
        
        // Ks: Specular reflectivity (RGB 0.0-1.0)
        aiColor3D Ks(0.0f, 0.0f, 0.0f);
        aiMat->Get(AI_MATKEY_COLOR_SPECULAR, Ks);
        glm::vec3 specular(Ks.r, Ks.g, Ks.b);
        
        // Ke: Emissive color (RGB 0.0+, typically 0.0-1.0)
        aiColor3D Ke(0.0f, 0.0f, 0.0f);
        aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, Ke);
        glm::vec3 emission(Ke.r, Ke.g, Ke.b);
        
        // Tf: Transmission filter (RGB 0.0-1.0) - filters light passing through
        // Note: Assimp stores this as COLOR_TRANSPARENT
        aiColor3D Tf(1.0f, 1.0f, 1.0f);
        aiMat->Get(AI_MATKEY_COLOR_TRANSPARENT, Tf);
        glm::vec3 transmissionFilter(Tf.r, Tf.g, Tf.b);
        
        // Ns: Specular exponent (0-1000, typical range)
        // Higher values = tighter, more focused highlights
        float Ns = 0.0f;
        aiMat->Get(AI_MATKEY_SHININESS, Ns);
        
        // d: Dissolve factor (0.0-1.0)
        // 1.0 = fully opaque, 0.0 = fully transparent
        float d = 1.0f;
        aiMat->Get(AI_MATKEY_OPACITY, d);
        
        // Ni: Optical density / Index of Refraction (0.001-10)
        // Air=1.0, Glass≈1.5, Water≈1.33, Diamond≈2.42
        // Only meaningful for refractive materials (illum 4,6,7,9)
        float Ni = 1.5f;
        aiMat->Get(AI_MATKEY_REFRACTI, Ni);
        if (Ni < 0.001f) Ni = 1.5f; // Clamp to valid range
        if (Ni > 10.0f) Ni = 10.0f;
        
        // illum: Illumination model (0-10)
        // This is THE authoritative property for material classification
        int illum = 2; // Default: Diffuse + Specular (most common)
        aiMat->Get(AI_MATKEY_SHADING_MODEL, illum);
        
        // ========== MTL ILLUMINATION MODEL CLASSIFICATION ==========
        // Reference: MTL Specification page 5-7
        // 
        // illum 0: Color on, Ambient off (flat color, no lighting)
        // illum 1: Color on, Ambient on (Lambertian diffuse)
        // illum 2: Highlight on (Diffuse + Blinn-Phong specular) [MOST COMMON]
        // illum 3: Reflection on, Ray trace on (reflective metal/mirror)
        // illum 4: Transparency: Glass on, Reflection: Ray trace on
        // illum 5: Reflection: Fresnel on, Ray trace on (perfect mirror)
        // illum 6: Transparency: Refraction on, Reflection: Fresnel off, Ray trace on
        // illum 7: Transparency: Refraction on, Reflection: Fresnel on, Ray trace on (realistic glass)
        // illum 8: Reflection on, Ray trace off (fake reflection)
        // illum 9: Transparency: Glass on, Reflection: Ray trace off (fake glass)
        // illum 10: Casts shadows onto invisible surfaces (shadow matte)
        
        std::shared_ptr<Material> mat;
        
        // Calculate emission intensity for detection
        float emissionIntensity = std::max(std::max(emission.r, emission.g), emission.b);
        
        // CLASSIFICATION PRIORITY (strict illum-based):
        
        // 1. Emissive materials (any illum with Ke > 0)
        if (emissionIntensity > 0.01f) {
            mat = std::make_shared<Material>();
            mat->SetType(MaterialType::Emissive);
            mat->SetAlbedo(diffuse);  // Use Kd as base color
            mat->SetEmission(emission);
            mat->SetSpecular(specular);
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Emissive" << std::endl;
            std::cout << "  illum=" << illum << ", Ke=(" << emission.x << "," << emission.y << "," << emission.z << ")" << std::endl;
        }
        // 2. Refractive/Transmissive materials (illum 4, 6, 7, 9)
        else if (illum == 4 || illum == 6 || illum == 7 || illum == 9) {
            mat = std::make_shared<TransmissiveMaterial>(diffuse, Ni);
            mat->SetAlbedo(diffuse);
            mat->SetSpecular(specular);
            mat->SetIOR(Ni);
            mat->SetTransmission(1.0f - d); // Dissolve to transmission
            mat->SetRoughness(Ns > 0.0f ? std::sqrt(2.0f / (Ns + 2.0f)) : 0.05f);
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Transmissive (Glass/Refractive)" << std::endl;
            std::cout << "  illum=" << illum << ", Ni=" << Ni << ", d=" << d << ", Ns=" << Ns << std::endl;
        }
        // 3. Mirror/Reflective materials (illum 3, 5, 8)
        else if (illum == 3 || illum == 5 || illum == 8) {
            mat = std::make_shared<SpecularMaterial>(specular, 0.0f);
            mat->SetAlbedo(diffuse);  // For colored mirrors
            mat->SetSpecular(specular); // Mirror reflectance color
            mat->SetRoughness(0.0f);  // Perfect mirror
            mat->SetMetallic(1.0f);
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Mirror/Reflective" << std::endl;
            std::cout << "  illum=" << illum << ", Ks=(" << specular.x << "," << specular.y << "," << specular.z << ")" << std::endl;
        }
        // 4. Standard Diffuse/Specular materials (illum 0, 1, 2, default)
        else {
            mat = std::make_shared<DiffuseMaterial>(diffuse);
            mat->SetAlbedo(diffuse);
            mat->SetSpecular(specular);
            // Convert Phong exponent (Ns) to roughness for PBR
            // Formula from Disney: roughness = sqrt(2/(Ns+2))
            float roughness = (Ns > 0.0f) ? std::sqrt(2.0f / (Ns + 2.0f)) : 0.5f;
            mat->SetRoughness(roughness);
            mat->SetMetallic(0.0f); // Diffuse materials are non-metallic
            mat->SetIllum(illum);
            
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": ";
            if (illum == 0) std::cout << "Flat Color (no lighting)";
            else if (illum == 1) std::cout << "Diffuse (Lambertian)";
            else if (illum == 2) std::cout << "Diffuse+Specular (Blinn-Phong)";
            else std::cout << "Diffuse (illum=" << illum << ")";
            std::cout << std::endl;
            
            std::cout << "  Ka=(" << ambient.x << "," << ambient.y << "," << ambient.z << ")";
            std::cout << ", Kd=(" << diffuse.x << "," << diffuse.y << "," << diffuse.z << ")";
            if (illum == 2 && Ns > 0.0f) {
                std::cout << ", Ks=(" << specular.x << "," << specular.y << "," << specular.z << ")";
                std::cout << ", Ns=" << Ns;
            }
            std::cout << std::endl;
        }
        
        // Load textures
        if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string fullPath = directory + texPath.C_Str();
                std::cout << "\n  Loading diffuse texture: " << fullPath << std::endl;
                auto texture = TextureManager::Instance().Load(fullPath);
                if (texture) {
                    mat->SetAlbedoTexture(texture);
                    std::cout << "  Texture loaded: " << texture->GetWidth() << "x" << texture->GetHeight() 
                             << " (" << texture->GetChannels() << " channels)" << std::endl;
                } else {
                    std::cerr << "  ERROR: Failed to load texture!" << std::endl;
                }
            }
        } else {
            std::cout << std::endl;  // End the material line
        }
        
        // Load normal map
        if (aiMat->GetTextureCount(aiTextureType_NORMALS) > 0) {
            aiString texPath;
            if (aiMat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
                std::string fullPath = directory + texPath.C_Str();
                auto texture = TextureManager::Instance().Load(fullPath);
                if (texture) {
                    mat->SetNormalTexture(texture);
                }
            }
        }
        
        // Load roughness map
        if (aiMat->GetTextureCount(aiTextureType_SHININESS) > 0) {
            aiString texPath;
            if (aiMat->GetTexture(aiTextureType_SHININESS, 0, &texPath) == AI_SUCCESS) {
                std::string fullPath = directory + texPath.C_Str();
                auto texture = TextureManager::Instance().Load(fullPath);
                if (texture) {
                    mat->SetRoughnessTexture(texture);
                }
            }
        }
        
        m_materials.push_back(mat);
    }

    auto processMesh = [&](aiMesh* aimesh) {
        std::vector<Vertex> vertices;
        vertices.reserve(aimesh->mNumVertices);
        for (unsigned int v = 0; v < aimesh->mNumVertices; ++v) {
            Vertex vert;
            vert.position = glm::vec3(aimesh->mVertices[v].x, aimesh->mVertices[v].y, aimesh->mVertices[v].z);
            if (aimesh->HasNormals()) {
                vert.normal = glm::vec3(aimesh->mNormals[v].x, aimesh->mNormals[v].y, aimesh->mNormals[v].z);
            } else {
                vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (aimesh->HasTextureCoords(0)) {
                vert.texCoord = glm::vec2(aimesh->mTextureCoords[0][v].x, aimesh->mTextureCoords[0][v].y);
            } else {
                vert.texCoord = glm::vec2(0.0f);
            }
            if (aimesh->HasTangentsAndBitangents()) {
                vert.tangent = glm::vec3(aimesh->mTangents[v].x, aimesh->mTangents[v].y, aimesh->mTangents[v].z);
            } else {
                vert.tangent = glm::vec3(0.0f);
            }
            vertices.push_back(vert);
        }
        std::vector<uint32_t> indices;
        indices.reserve(aimesh->mNumFaces * 3);
        for (unsigned int f = 0; f < aimesh->mNumFaces; ++f) {
            const aiFace& face = aimesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            indices.push_back(face.mIndices[0]);
            indices.push_back(face.mIndices[1]);
            indices.push_back(face.mIndices[2]);
        }
        auto mesh = std::make_shared<Mesh>();
        mesh->SetVertices(vertices);
        mesh->SetIndices(indices);
        mesh->SetMaterialIndex(static_cast<int>(aimesh->mMaterialIndex));
        m_meshes.push_back(mesh);
    };

    std::function<void(aiNode*)> processNode = [&](aiNode* node) {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            aiMesh* aimesh = ascene->mMeshes[node->mMeshes[i]];
            processMesh(aimesh);
        }
        for (unsigned int c = 0; c < node->mNumChildren; ++c) {
            processNode(node->mChildren[c]);
        }
    };
    processNode(ascene->mRootNode);

    ComputeBoundingBox();
    std::cout << "Loaded meshes: " << m_meshes.size() << ", materials: " << m_materials.size() << std::endl;
    std::cout << "Scene bounding box: min=(" << m_bboxMin.x << ", " << m_bboxMin.y << ", " << m_bboxMin.z << ")"
             << " max=(" << m_bboxMax.x << ", " << m_bboxMax.y << ", " << m_bboxMax.z << ")" << std::endl;
    
    // Calculate scene center and size
    glm::vec3 center = (m_bboxMin + m_bboxMax) * 0.5f;
    glm::vec3 size = m_bboxMax - m_bboxMin;
    float maxDim = std::max({size.x, size.y, size.z});
    
    std::cout << "Scene center: (" << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;
    std::cout << "Scene size: " << size.x << " x " << size.y << " x " << size.z 
             << " (max: " << maxDim << ")" << std::endl;
    
    // Give camera positioning hints
    if (maxDim < 1.0f) {
        std::cout << "HINT: This is a small scene. Recommended camera distance: " << (maxDim * 2.0f) << std::endl;
    } else if (maxDim > 100.0f) {
        std::cout << "HINT: This is a large scene. Recommended camera distance: " << (maxDim * 1.5f) << std::endl;
    }
    
    return true;
}

void Scene::ComputeBoundingBox() {
    m_bboxMin = glm::vec3(std::numeric_limits<float>::max());
    m_bboxMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (auto& mesh : m_meshes) {
        const auto& verts = mesh->GetVertices();
        for (const auto& v : verts) {
            m_bboxMin = glm::min(m_bboxMin, v.position);
            m_bboxMax = glm::max(m_bboxMax, v.position);
        }
    }
}

} // namespace ACG
