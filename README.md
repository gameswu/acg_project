# ACG Project
<div align="center">
<i>2025 Fall Advanced Computer Graphics</i>
</div>

This repository contains the code and resources for the Advanced Computer Graphics (ACG) project for the Fall 2025 semester. The project focuses on ray tracing techniques and rendering algorithms.

## Targets and Course Requirements

We are planning to implement a GPU-based renderer that meets the following course requirements:
<details>
<summary>Click to expand the requirements</summary>

- Base: Implement a path tracing algorithm that correctly handles diffuse and specular materials. (basic)
- Scene creation: Build a custom scene with aesthetic considerations, using geometry that you create from scratch or find online (ensure the source is credited). (basic, tidiness and attractiveness 1pt)
- Acceleration structure: Implement an acceleration structure such as BVH (Bounding Volume Hierarchy). This is not required for hardware-based renderers, as the acceleration structure is built-in in that case. (basic, Surface Area Heuristic or another advanced algorithm 2pts)
- Material: Create a (non-trivial) custom material. Options include:
    - Transmissive material (basic)
    - Principled BSDF (2pts)
    - Multi-layer material (2pts)
    - Rendering of fur, hair, skin, etc. (2pts)
- Texture: Create your own (non-trivial) texture with proper texture mapping. Options include:
    - Color texture (basic)
    - Normal map, height map, attribute map, or any functional texture mapping (1pt for each, up to 2pts)
    - Implement an adaptive mipmap algorithm (2pts)
- Importance Sampling: Use more advanced sampling algorithms for path tracing. (Importance sampling with Russian Roulette, multiple importance sampling 2pts)
- Volumetric Rendering: Options include:
    - Subsurface scattering (2pts)
    - Homogeneous volume rendering (1pt)
    - Inhomogeneous volume rendering (1pt)
    - Channel-independent subsurface scattering (1pt)
    - Volumetric emission (1pt)
    - Volumetric alpha shadow (2pts)
- Special Visual Effects: Options include:
    - Motion blur, depth of field (basic)
    - Alpha shadow (basic)
    - Cartoon style rendering (2pts)
    - Chromatic dispersion (2pts)
- Lighting: Options include:
    - Point light and area light (basic)
    - Environment lighting with HDR, such as skybox (2pts)
- Anti-aliasing: Implement an anti-aliasing algorithm (basic)
- Simulation-based content creation: Up to 2pts
</details>

Details of each requirement can be found in the [project description document](/docs/ACG_2025_Project_Announcement.pdf).

## Repository Structure
```
acg_project/
├── include/             # Header directory
│   ├── Camera.h         # Camera system
│   ├── DX12Helper.h     # DirectX 12 helper functions
│   ├── Light.h          # Lighting system (point light, area light, environment light)
│   ├── LogRedirector.h  # Redirect console output to GUI log panel
│   ├── Material.h       # Material system (diffuse, specular, transmissive, PBR)
│   ├── MathUtils.h      # Math utility functions
│   ├── Mesh.h           # Geometry mesh
│   ├── Renderer.h       # GPU renderer (DirectX 11)
│   ├── Sampler.h        # Sampler (importance sampling, MIS)
│   ├── Scene.h          # Scene management
│   └── Texture.h        # Texture system (supports Mipmap)
│
├── src/                 # Source files directory
│   ├── Camera.cpp
│   ├── Light.cpp
│   ├── Material.cpp
│   ├── MathUtils.cpp
│   ├── Mesh.cpp
│   ├── Renderer.cpp·
│   ├── Sampler.cpp
│   ├── Scene.cpp
│   ├── Texture.cpp
│   └── main.cpp         # Main program entry point
│
├── shaders/             # Shader files directory
│   ├── Raytracing.hlsl  # HLSL shader for ray tracing
│   ├── Random.hlsli     # HLSL shader for random number generation
│   └── Structures.hlsli # HLSL shader structures
│
├── lib/                 # Third-party libraries
│   └── WinPixEventRuntime/ # PIX for Windows library
├── docs/                # Documentation directory
├── tests/               # Test scenes and scripts
├── CMakeLists.txt       # CMake build configuration
├── vcpkg.json           # vcpkg dependency configuration
└── README.md            # Project description
```

## Usage

We provide a GUI for users to configure rendering settings and load scenes. The GUI allows you to adjust parameters such as samples per pixel, maximum bounces, and load different 3D models.

<div align="center">
    <img src="docs/images/gui.png" alt="GUI Screenshot" width="800"/>
</div>

- **Render Settings:** Adjust output resolution, sampling parameters, lighting intensity, and scene model paths.
- **Camera Settings:** Configure camera position, target, up vector, and field of view.
- **Render Statistics:** Monitor rendering progress, samples, and performance metrics.
- **Controls:** Start or stop rendering processes.
- **Log Details:** View log messages and debug information.

## Development
### Dependencies

This project requires the following dependencies:
- CMake >= 3.15
- A C++17 compatible compiler
- vcpkg for managing third-party libraries
- Windows SDK for DirectX libraries

### Building the Project

To build the project, follow these steps:
1. Clone the repository:
    ```bash
    git clone https://github.com/gameswu/acg_project.git
    cd acg_project
    ```
2. Configure and build the project using CMake:
    ```bash
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    cmake --build build --config Release
    ```

### Running tests

The test scenes are downloaded from https://casual-effects.com/data/ by the script.
```bash
python -m venv .venv
.venv\Scripts\activate # On Windows
pip install -r requirements.txt
python tests/download.py
```

| Test Scene | Targeted Feature |
|------------|------------------|
| CornellBox | Basic rendering test |
| breakfast_room | Texture mapping |
| sponza | Large scene |
| lpshead | Head model |
| hairball | Hair model |