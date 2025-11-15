#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include "Renderer.h"
#include "Scene.h"
#include "Camera.h"
#include "Mesh.h"
#include "Material.h"
#include "Light.h"

using namespace ACG;
namespace fs = std::filesystem;

static bool SavePPM(const std::string& filename, int width, int height, const unsigned char* rgba)
{
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    // Convert RGBA to RGB while writing
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = &rgba[(y * width + x) * 4];
            ofs.put(static_cast<char>(p[0]));
            ofs.put(static_cast<char>(p[1]));
            ofs.put(static_cast<char>(p[2]));
        }
    }
    return true;
}

static std::string FindSceneModelPath(const std::string& input)
{
    std::vector<std::string> exts = {".obj", ".fbx", ".gltf", ".glb", ".dae"};
    fs::path p = input.empty() ? fs::path("tests/scenes") : fs::path(input);
    std::error_code ec;
    
    if (fs::exists(p, ec)) {
        if (fs::is_regular_file(p, ec)) {
            return p.string();
        }
        if (fs::is_directory(p, ec)) {
            // Collect all model files
            std::vector<std::string> modelFiles;
            for (auto& entry : fs::recursive_directory_iterator(p, ec)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    for (auto& e : exts) {
                        if (ext == e) {
                            modelFiles.push_back(entry.path().string());
                            break;
                        }
                    }
                }
            }
            
            if (!modelFiles.empty()) {
                std::cout << "Found " << modelFiles.size() << " model file(s) in directory:" << std::endl;
                for (size_t i = 0; i < modelFiles.size(); ++i) {
                    std::cout << "  [" << i << "] " << modelFiles[i] << std::endl;
                }
                
                // Prefer "Original" or first file with most complete name
                for (const auto& file : modelFiles) {
                    if (file.find("Original") != std::string::npos) {
                        std::cout << "Auto-selecting: " << file << std::endl;
                        return file;
                    }
                }
                
                std::cout << "Auto-selecting: " << modelFiles[0] << std::endl;
                return modelFiles[0];
            }
        }
    }
    return std::string();
}

int main(int argc, char** argv) {
    std::cout << "=== ACG Path Tracing Renderer ===" << std::endl;
    std::cout << "Advanced Computer Graphics Project - Fall 2025" << std::endl;
    std::cout << std::endl;

    // Rendering parameters
    const int width = 1280;
    const int height = 720;
    const int samplesPerPixel = 10000;
    const int maxBounces = 5;

    // Determine scene path
    std::string sceneArg = (argc > 1) ? std::string(argv[1]) : std::string();
    std::string modelPath = FindSceneModelPath(sceneArg);
    if (modelPath.empty()) {
        std::cerr << "No model found. Usage:" << std::endl;
        std::cerr << "  " << argv[0] << " <model.obj>           # Load specific file" << std::endl;
        std::cerr << "  " << argv[0] << " <directory>           # Auto-select from directory" << std::endl;
        std::cerr << "  " << argv[0] << " tests/scenes/CornellBox/CornellBox-Original.obj" << std::endl;
        return 1;
    }
    std::cout << "\nUsing model: " << modelPath << std::endl << std::endl;

    // Initialize renderer
    std::cout << "Initializing renderer..." << std::endl;
    Renderer renderer;
    if (!renderer.Initialize(width, height)) {
        std::cerr << "Failed to initialize renderer!" << std::endl;
        return 1;
    }
    renderer.SetSamplesPerPixel(samplesPerPixel);
    renderer.SetMaxBounces(maxBounces);
    renderer.SetRussianRouletteDepth(3);

    // Create scene
    std::cout << "Building scene..." << std::endl;
    Scene scene;
    if (!scene.LoadFromFile(modelPath)) {
        std::cerr << "Failed to load scene from: " << modelPath << std::endl;
        return 1;
    }

    // Optionally add lights if scene has none
    // (Cornell Box has emissive materials built-in, so we skip this)
    if (scene.GetLights().empty()) {
        std::cout << "Note: No explicit light sources. Using emissive materials." << std::endl;
    }
    
    scene.ComputeBoundingBox();
    std::cout << "Scene bounds: [" 
              << scene.GetBBoxMin().x << ", " << scene.GetBBoxMin().y << ", " << scene.GetBBoxMin().z << "] to ["
              << scene.GetBBoxMax().x << ", " << scene.GetBBoxMax().y << ", " << scene.GetBBoxMax().z << "]" << std::endl;

    // Setup camera
    std::cout << "Setting up camera..." << std::endl;
    Camera camera;
    
    // For Cornell Box: position camera INSIDE the box looking at center
    // Box bounds: [-1.02, 0, -1.04] to [1, 1.99, 0.99]
    glm::vec3 sceneCenter = (scene.GetBBoxMin() + scene.GetBBoxMax()) * 0.5f;
    
    // Position camera for Cornell Box
    // Standard Cornell Box: camera looks into the box from the front opening
    // Scene bounds: [-1.02, 0, -1.04] to [1, 1.99, 0.99]
    glm::vec3 cameraPos = glm::vec3(0.0f, 1.0f, 2.7f);  // Outside front, mid-height
    glm::vec3 cameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);  // Look at center of box
    
    camera.SetPosition(cameraPos);
    camera.SetTarget(cameraTarget);
    camera.SetFOV(60.0f);  // Standard Cornell Box FOV
    camera.SetAspectRatio(static_cast<float>(width) / height);
    camera.SetAperture(0.0f);  // 0 = no depth of field
    camera.SetFocusDistance(glm::length(cameraPos - cameraTarget));
    
    std::cout << "Camera position: [" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << "]" << std::endl;
    std::cout << "Camera target: [" << cameraTarget.x << ", " << cameraTarget.y << ", " << cameraTarget.z << "]" << std::endl;

    // Render
    std::cout << "Rendering..." << std::endl;
    std::cout << "Resolution: " << width << "x" << height << std::endl;
    std::cout << "Samples per pixel: " << samplesPerPixel << std::endl;
    std::cout << "Max bounces: " << maxBounces << std::endl;
    std::cout << std::endl;

    // Simple single-frame render call (compute shader path placeholder)
    renderer.Render(scene, camera);

    // Get result and save
    std::vector<unsigned char> pixels(width * height * 4);
    renderer.GetRenderResult(pixels.data());
    std::string outName = "output.ppm";
    if (SavePPM(outName, width, height, pixels.data())) {
        std::cout << "Saved image: " << outName << std::endl;
    } else {
        std::cerr << "Failed to save image: " << outName << std::endl;
    }

    std::cout << "Rendering complete!" << std::endl;
    std::cout << "Project framework ready for implementation." << std::endl;

    return 0;
}
