#include "Scene.h"
#include "MTLParser.h"
#include <iostream>
#include <limits>
#include <filesystem>
#include <unordered_map>
#include <algorithm>

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

bool Scene::LoadFromFile(const std::string& filename, bool useCustomMTLParser) {
    std::cout << "Loading scene from: " << filename << std::endl;
    
    // Check file extension
    std::string extension = filename.substr(filename.find_last_of('.'));
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    bool isObjFile = (extension == ".obj");
    
    // For non-OBJ files, always use Assimp
    if (!isObjFile) {
        useCustomMTLParser = false;
        std::cout << "Non-OBJ file detected, using Assimp for all loading" << std::endl;
    }
    
    std::cout << "MTL Parser: " << (useCustomMTLParser ? "Custom MTL Parser" : "Assimp") << std::endl;
    
    // Extract directory from filename for texture loading and scene name
    std::string directory;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        directory = filename.substr(0, lastSlash + 1);
        
        // Extract scene name from file path
        size_t lastDot = filename.find_last_of(".");
        if (lastDot != std::string::npos && lastDot > lastSlash) {
            m_name = filename.substr(lastSlash + 1, lastDot - lastSlash - 1);
        } else {
            m_name = "Unnamed Scene";
        }
    } else {
        m_name = "Unnamed Scene";
    }
    
    // ========== LOAD GEOMETRY ONLY WITH ASSIMP ==========
    // Assimp is ONLY used for geometry (vertices, indices, normals)
    // All material loading is done through custom MTL parser
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
    
    std::unordered_map<std::string, std::shared_ptr<Material>> materialMap;
    std::vector<std::string> materialOrder; // Preserve material order
    
    // ========== MATERIAL LOADING ==========
    if (useCustomMTLParser) {
        // ========== CUSTOM MTL PARSER - ONLY MATERIAL LOADING METHOD ==========
        // Parse MTL file directly for 100% accurate material properties
        // This completely bypasses Assimp's buggy material parsing
        std::string mtlFilename = filename.substr(0, filename.find_last_of('.')) + ".mtl";
        
        if (std::filesystem::exists(mtlFilename)) {
            std::cout << "\n========== Custom MTL Parser (v1.0) ==========" << std::endl;
            std::cout << "MTL File: " << mtlFilename << std::endl;
            std::cout << "MTL Specification: Wavefront v4.2 (October 1995)" << std::endl;
            
            MTLParser parser;
            auto parsedMaterials = parser.Parse(mtlFilename);
            
            // Extract MTL directory for texture loading
            std::filesystem::path mtlPath(mtlFilename);
            std::string mtlDirectory = mtlPath.parent_path().string();
            
            std::cout << "\nParsed " << parsedMaterials.size() << " materials from MTL file:\n" << std::endl;
        
        for (const auto& mtl : parsedMaterials) {
            auto mat = MTLParser::ConvertToMaterial(mtl, mtlDirectory);
            materialMap[mtl.name] = mat;
            materialOrder.push_back(mtl.name);
            
            // Log material details
            std::cout << "Material: \"" << mtl.name << "\"" << std::endl;
            std::cout << "  Type: ";
            switch (mat->GetType()) {
                case MaterialType::Emissive: std::cout << "Emissive"; break;
                case MaterialType::Transmissive: std::cout << "Transmissive"; break;
                case MaterialType::Diffuse: std::cout << "Diffuse"; break;
                case MaterialType::Specular: std::cout << "Specular"; break;
                default: std::cout << "Unknown"; break;
            }
            std::cout << std::endl;
            
            std::cout << "  illum: " << mtl.illum;
            std::cout << ", Kd: (" << mtl.Kd.x << ", " << mtl.Kd.y << ", " << mtl.Kd.z << ")";
            
            if (mat->GetType() == MaterialType::Emissive) {
                glm::vec3 emission = mat->GetEmission();
                std::cout << ", Ke: (" << emission.x << ", " << emission.y << ", " << emission.z << ")";
            }
            
            if (mat->GetType() == MaterialType::Specular) {
                glm::vec3 specular = mat->GetSpecular();
                std::cout << ", Ks: (" << specular.x << ", " << specular.y << ", " << specular.z << ")";
            }
            
            if (mat->GetType() == MaterialType::Transmissive) {
                std::cout << ", Ni: " << mat->GetIOR();
            }
            
            std::cout << std::endl;
        }
        
        std::cout << "\n============================================\n" << std::endl;
        } else {
            std::cerr << "ERROR: MTL file not found: " << mtlFilename << std::endl;
            std::cerr << "Custom MTL parser is REQUIRED for material loading." << std::endl;
            return false;
        }
    } else {
        // ========== USE ASSIMP FOR MATERIAL LOADING ==========
        std::cout << "\n========== Using Assimp Material Parser ==========" << std::endl;
        std::cout << "Loading materials via Assimp library" << std::endl;
        
        for (unsigned int i = 0; i < ascene->mNumMaterials; ++i) {
            aiMaterial* aiMat = ascene->mMaterials[i];
            
            aiString name;
            aiMat->Get(AI_MATKEY_NAME, name);
            std::string materialName = name.C_Str();
            
            // Load diffuse color
            aiColor3D diffuse(0.8f, 0.8f, 0.8f);
            aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
            glm::vec3 diffuseColor(diffuse.r, diffuse.g, diffuse.b);
            
            // Create diffuse material with the color
            auto mat = std::make_shared<DiffuseMaterial>(diffuseColor);
            
            materialMap[materialName] = mat;
            materialOrder.push_back(materialName);
            
            std::cout << "Material: \"" << materialName << "\" loaded via Assimp (Diffuse)" << std::endl;
        }
        
        std::cout << "============================================\n" << std::endl;
    }
    
    // ========== MAP MATERIALS TO SCENE ==========
    // Map materials in the order they appear in the scene
    if (useCustomMTLParser) {
        // For custom MTL parser: map Assimp material names to our parsed materials
        for (unsigned int i = 0; i < ascene->mNumMaterials; ++i) {
            aiMaterial* aiMat = ascene->mMaterials[i];
            
            aiString name;
            aiMat->Get(AI_MATKEY_NAME, name);
            std::string materialName = name.C_Str();
            
            auto it = materialMap.find(materialName);
            if (it != materialMap.end()) {
                m_materials.push_back(it->second);
                std::cout << "Material[" << i << "] \"" << materialName << "\": Loaded from custom MTL parser" << std::endl;
            } else {
                // Material not found in MTL file - create default diffuse
                std::cerr << "WARNING: Material \"" << materialName << "\" not found in MTL file" << std::endl;
                std::cerr << "         Using default gray diffuse material" << std::endl;
                auto defaultMat = std::make_shared<DiffuseMaterial>(glm::vec3(0.5f));
                m_materials.push_back(defaultMat);
            }
        }
    } else {
        // For Assimp: materials are already in materialMap, just add them in order
        for (const auto& matName : materialOrder) {
            auto it = materialMap.find(matName);
            if (it != materialMap.end()) {
                m_materials.push_back(it->second);
            }
        }
    }
    
    // If no materials at all, add default
    if (m_materials.empty()) {
        std::cout << "No materials loaded, adding default material" << std::endl;
        auto defaultMat = std::make_shared<DiffuseMaterial>(glm::vec3(0.8f));
        m_materials.push_back(defaultMat);
    }
    
    // ========== LOAD GEOMETRY ==========
    // Process meshes (geometry only, materials already loaded)
    ProcessNode(ascene->mRootNode, ascene);
    
    CalculateBoundingBox();
    
    std::cout << "\n========== Scene Loading Complete ==========" << std::endl;
    std::cout << "Meshes: " << m_meshes.size() << std::endl;
    std::cout << "Materials: " << m_materials.size() << std::endl;
    std::cout << "Bounding box: min=(" << m_bboxMin.x << ", " << m_bboxMin.y << ", " << m_bboxMin.z << ")" 
              << " max=(" << m_bboxMax.x << ", " << m_bboxMax.y << ", " << m_bboxMax.z << ")" << std::endl;
    
    glm::vec3 center = (m_bboxMin + m_bboxMax) * 0.5f;
    glm::vec3 size = m_bboxMax - m_bboxMin;
    float maxSize = std::max({size.x, size.y, size.z});
    
    std::cout << "Center: (" << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;
    std::cout << "Size: " << size.x << " x " << size.y << " x " << size.z << " (max: " << maxSize << ")" << std::endl;
    std::cout << "============================================\n" << std::endl;
    
    return true;
}

void Scene::ProcessNode(aiNode* node, const aiScene* scene) {
    // Process meshes
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* aimesh = scene->mMeshes[node->mMeshes[i]];
        ProcessMesh(aimesh, scene);
    }
    
    // Recursively process child nodes
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene);
    }
}

void Scene::ProcessMesh(aiMesh* aimesh, const aiScene* scene) {
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
}

void Scene::CalculateBoundingBox() {
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

void Scene::ComputeBoundingBox() {
    CalculateBoundingBox();
}

} // namespace ACG
