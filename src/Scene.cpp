#include "Scene.h"
#include <iostream>
#include <limits>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

namespace ACG {

Scene::Scene() 
    : m_bboxMin(std::numeric_limits<float>::max())
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
        
        // Create material based on properties
        std::shared_ptr<Material> mat;
        float emissiveIntensity = glm::length(emissive);
        if (emissiveIntensity > 0.01f) {
            // Emissive material (light source)
            mat = std::make_shared<Material>();
            mat->SetType(MaterialType::Emissive);
            mat->SetAlbedo(albedo);
            mat->SetEmission(emissive);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Emissive (Ke=" 
                     << emissive.x << "," << emissive.y << "," << emissive.z << ")" << std::endl;
        } else {
            // Diffuse material
            mat = std::make_shared<DiffuseMaterial>(albedo);
            std::cout << "Material[" << i << "] \"" << name.C_Str() << "\": Diffuse (Kd=" 
                     << albedo.x << "," << albedo.y << "," << albedo.z << ")" << std::endl;
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
