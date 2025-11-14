#include <iostream>
#include "Renderer.h"
#include "Scene.h"
#include "Camera.h"
#include "Mesh.h"
#include "Material.h"
#include "Light.h"

using namespace ACG;

int main() {
    std::cout << "=== ACG Path Tracing Renderer ===" << std::endl;
    std::cout << "Advanced Computer Graphics Project - Fall 2025" << std::endl;
    std::cout << std::endl;

    // Rendering parameters
    const int width = 1280;
    const int height = 720;
    const int samplesPerPixel = 100;
    const int maxBounces = 5;

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
    
    // TODO: Add meshes to scene
    // auto sphere = Mesh::CreateSphere(1.0f, 32);
    // sphere->SetMaterialIndex(0);
    // scene.AddMesh(sphere);
    
    // TODO: Add materials
    // auto diffuseMat = std::make_shared<DiffuseMaterial>(glm::vec3(0.8f, 0.2f, 0.2f));
    // scene.AddMaterial(diffuseMat);
    
    // TODO: Add lights
    // auto pointLight = std::make_shared<PointLight>();
    // pointLight->SetPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    // pointLight->SetIntensity(100.0f);
    // scene.AddLight(pointLight);
    
    scene.ComputeBoundingBox();

    // Setup camera
    std::cout << "Setting up camera..." << std::endl;
    Camera camera;
    camera.SetPosition(glm::vec3(0.0f, 2.0f, 5.0f));
    camera.SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.SetFOV(45.0f);
    camera.SetAspectRatio(static_cast<float>(width) / height);
    camera.SetAperture(0.0f);  // 0 = no depth of field
    camera.SetFocusDistance(5.0f);

    // Render
    std::cout << "Rendering..." << std::endl;
    std::cout << "Resolution: " << width << "x" << height << std::endl;
    std::cout << "Samples per pixel: " << samplesPerPixel << std::endl;
    std::cout << "Max bounces: " << maxBounces << std::endl;
    std::cout << std::endl;

    // TODO: Implement rendering loop
    // renderer.Render(scene, camera);

    // Get result
    // std::vector<unsigned char> pixels(width * height * 4);
    // renderer.GetRenderResult(pixels.data());

    // TODO: Save image to file
    // SaveImage("output.png", width, height, pixels.data());

    std::cout << "Rendering complete!" << std::endl;
    std::cout << "Project framework ready for implementation." << std::endl;

    return 0;
}
