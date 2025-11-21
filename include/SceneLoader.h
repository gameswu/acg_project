/*
 * Scene Loader - Binary Format
 * 高性能二进制场景加载器
 * 直接从.acg二进制文件加载场景数据
 */

#pragma once
#include <fstream>
#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <type_traits>
#include "Scene.h"
#include "Material.h"
#include "MaterialLayers.h"
#include "Mesh.h"
#include "Texture.h"

namespace ACG {

class SceneLoader {
public:
    static constexpr uint32_t MAGIC = 0x53474341;  // 'ACGS' in little-endian
    static constexpr uint32_t VERSION = 1;

    static std::unique_ptr<Scene> Load(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open binary scene file: " + filepath);
        }

        // 验证文件头
        uint32_t magic, version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != MAGIC) {
            throw std::runtime_error("Invalid binary scene file format");
        }
        if (version != VERSION) {
            throw std::runtime_error("Unsupported binary scene version");
        }

        auto scene = std::make_unique<Scene>();

        // 读取材质、纹理、网格(先加载纹理,再关联到材质)
        std::vector<std::shared_ptr<Texture>> textures;
        std::vector<std::array<int32_t, 4>> materialTexIndices;  // 暂存材质的纹理索引
        LoadMaterials(file, scene.get(), materialTexIndices);
        LoadTextures(file, scene.get(), textures);
        
        // 关联纹理到材质
        const auto& materials = scene->GetMaterials();
        for (size_t i = 0; i < materials.size() && i < materialTexIndices.size(); ++i) {
            const auto& texIndices = materialTexIndices[i];
            auto& mat = materials[i];
            
            if (texIndices[0] >= 0 && texIndices[0] < static_cast<int32_t>(textures.size())) {
                mat->SetBaseColorTexture(textures[texIndices[0]], texIndices[0]);
            }
            if (texIndices[1] >= 0 && texIndices[1] < static_cast<int32_t>(textures.size())) {
                mat->SetNormalTexture(textures[texIndices[1]], texIndices[1]);
            }
            if (texIndices[2] >= 0 && texIndices[2] < static_cast<int32_t>(textures.size())) {
                mat->SetMetallicRoughnessTexture(textures[texIndices[2]], texIndices[2]);
            }
            if (texIndices[3] >= 0 && texIndices[3] < static_cast<int32_t>(textures.size())) {
                mat->SetEmissionTexture(textures[texIndices[3]], texIndices[3]);
            }
        }
        
        LoadMeshes(file, scene.get());

        return scene;
    }

private:
    static std::string ReadString(std::ifstream& file) {
        uint32_t length;
        file.read(reinterpret_cast<char*>(&length), sizeof(length));
        
        if (length > 10000) {  // 防止异常大的字符串
            throw std::runtime_error("Invalid string length in binary file");
        }
        
        std::string str(length, '\0');
        if (length > 0) {
            file.read(&str[0], length);
        }
        return str;
    }

    static void LoadMaterials(std::ifstream& file, Scene* scene, 
                              std::vector<std::array<int32_t, 4>>& materialTexIndices) {
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (uint32_t i = 0; i < count; ++i) {
            auto mat = std::make_shared<Material>();

            // 名称
            mat->SetName(ReadString(file));

            // PBR参数（直接读取到内存）
            glm::vec3 baseColor, emission;
            float metallic, roughness, ior, opacity;

            file.read(reinterpret_cast<char*>(&baseColor), sizeof(baseColor));
            file.read(reinterpret_cast<char*>(&emission), sizeof(emission));
            file.read(reinterpret_cast<char*>(&metallic), sizeof(metallic));
            file.read(reinterpret_cast<char*>(&roughness), sizeof(roughness));
            file.read(reinterpret_cast<char*>(&ior), sizeof(ior));
            file.read(reinterpret_cast<char*>(&opacity), sizeof(opacity));

            mat->SetBaseColor(baseColor);
            mat->SetEmission(emission);
            mat->SetMetallic(metallic);
            mat->SetRoughness(roughness);
            mat->SetIOR(ior);
            mat->SetOpacity(opacity);

            // 纹理索引
            std::array<int32_t, 4> texIndices;
            file.read(reinterpret_cast<char*>(texIndices.data()), sizeof(int32_t) * 4);
            materialTexIndices.push_back(texIndices);  // 暂存,稍后关联
            
            // 材质层标志
            uint32_t flags;
            file.read(reinterpret_cast<char*>(&flags), sizeof(flags));

            // Transmission层
            if (flags & 0x01) {
                float strength, transmissionIOR;
                file.read(reinterpret_cast<char*>(&strength), sizeof(strength));
                file.read(reinterpret_cast<char*>(&transmissionIOR), sizeof(transmissionIOR));
                
                TransmissionLayer transLayer;
                transLayer.strength = strength;
                transLayer.roughness = 0.0f;  // 默认值
                transLayer.depth = 0.0f;
                transLayer.textureIdx = -1;
                transLayer.color = glm::vec3(1.0f);
                
                mat->SetTransmissionLayer(transLayer);
            }
            
            // Clearcoat层
            if (flags & 0x02) {
                // 跳过clearcoat数据（如果导出器写入了）
                // 暂不支持
            }
            
            // Sheen层
            if (flags & 0x04) {
                // 跳过sheen数据（如果导出器写入了）
                // 暂不支持
            }

            scene->AddMaterial(mat);
        }
    }

    static void LoadTextures(std::ifstream& file, Scene* scene,
                           std::vector<std::shared_ptr<Texture>>& textures) {
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        textures.reserve(count);
        
        for (uint32_t i = 0; i < count; ++i) {
            std::string texPath = ReadString(file);
            
            // 创建纹理对象并加载
            auto texture = std::make_shared<Texture>();
            
            // 检查文件是否存在
            std::filesystem::path fullPath = texPath;
            if (!std::filesystem::exists(fullPath)) {
                std::cerr << "Warning: Texture not found: " << texPath << std::endl;
            } else {
                // 立即加载纹理数据
                if (!texture->LoadFromFile(texPath)) {
                    std::cerr << "Error: Failed to load texture: " << texPath << std::endl;
                }
            }
            
            textures.push_back(texture);
        }
    }

    static void LoadMeshes(std::ifstream& file, Scene* scene) {
        uint32_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (uint32_t i = 0; i < count; ++i) {
            auto mesh = std::make_shared<Mesh>();

            // 名称
            mesh->SetName(ReadString(file));

            // 材质索引
            uint32_t matIndex;
            file.read(reinterpret_cast<char*>(&matIndex), sizeof(matIndex));
            mesh->SetMaterialIndex(static_cast<int>(matIndex));

            // 顶点数据
            uint32_t vertCount;
            file.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount));

            if (vertCount == 0) {
                throw std::runtime_error("Mesh has zero vertices: " + mesh->GetName());
            }

            std::vector<Vertex> vertices(vertCount);
            
            // 检查Vertex是否可以直接批量读取（需要验证内存布局）
            static_assert(std::is_standard_layout<Vertex>::value, 
                         "Vertex must be standard layout for binary loading");
            
            file.read(reinterpret_cast<char*>(vertices.data()), 
                     vertCount * sizeof(Vertex));
            
            if (!file.good()) {
                throw std::runtime_error("Failed to read vertex data for mesh: " + mesh->GetName());
            }
            
            mesh->SetVertices(vertices);

            // 索引数据
            uint32_t idxCount;
            file.read(reinterpret_cast<char*>(&idxCount), sizeof(idxCount));

            if (idxCount == 0 || idxCount % 3 != 0) {
                throw std::runtime_error("Invalid index count for mesh: " + mesh->GetName());
            }

            std::vector<uint32_t> indices(idxCount);
            file.read(reinterpret_cast<char*>(indices.data()), 
                     idxCount * sizeof(uint32_t));
            
            if (!file.good()) {
                throw std::runtime_error("Failed to read index data for mesh: " + mesh->GetName());
            }
            
            mesh->SetIndices(indices);

            scene->AddMesh(mesh);
        }
    }
};

} // namespace ACG
