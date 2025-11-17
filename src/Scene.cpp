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
        
        // Get material name
        aiString name;
        aiMat->Get(AI_MATKEY_NAME, name);
        
        // Get diffuse color (Kd)
        aiColor3D diffuse(0.8f, 0.8f, 0.8f);
        aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        glm::vec3 albedo(diffuse.r, diffuse.g, diffuse.b);
        
        // Get emission (Ke)
        aiColor3D emission(0.0f, 0.0f, 0.0f);
        aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, emission);
        glm::vec3 emissive(emission.r, emission.g, emission.b);
        
        // Get specular properties
        aiColor3D specular(0.0f, 0.0f, 0.0f);
        aiMat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
        float shininess = 0.0f;
        aiMat->Get(AI_MATKEY_SHININESS, shininess);
        int illum = 0;
        aiMat->Get(AI_MATKEY_SHADING_MODEL, illum);
        
        // Create material based on properties
        std::shared_ptr<Material> mat;
        float emissiveIntensity = glm::length(emissive);
        float specularIntensity = (specular.r + specular.g + specular.b) / 3.0f;
        
        if (emissiveIntensity > 0.01f) {
            // Emissive material (light source)
            mat = std::make_shared<Material>();
            mat->SetType(MaterialType::Emissive);
            mat->SetAlbedo(albedo);
            mat->SetEmission(emissive);
            mat->SetSpecular(glm::vec3(specular.r, specular.g, specular.b));
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Emissive (Ke=" 
                     << emissive.x << "," << emissive.y << "," << emissive.z << ")" << std::endl;
        } else if (specularIntensity > 0.5f || illum == 5) {
            // Specular/Mirror material (illum 5 is mirror reflection)
            mat = std::make_shared<SpecularMaterial>(albedo, 0.0f); // 0.0f roughness = perfect mirror
            mat->SetSpecular(glm::vec3(specular.r, specular.g, specular.b));
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Specular/Mirror (Ks=" 
                     << specular.r << "," << specular.g << "," << specular.b << ", illum=" << illum << ")" << std::endl;
        } else {
            // Diffuse material
            mat = std::make_shared<DiffuseMaterial>(albedo);
            mat->SetSpecular(glm::vec3(specular.r, specular.g, specular.b));
            mat->SetIllum(illum);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Diffuse (Kd=" 
                     << albedo.x << "," << albedo.y << "," << albedo.z << ")";
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
