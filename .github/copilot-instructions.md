# ACG Project - AI Coding Assistant Instructions

## Project Overview
This is a GPU-accelerated path tracing renderer built with DirectX 12 Raytracing (DXR) for an Advanced Computer Graphics course project. The renderer implements physically-based rendering with support for basic path tracing, diffuse/specular/transmissive materials, texture mapping, HDR environment lighting, anti-aliasing, and real-time denoising using Intel OIDN.

### Current Implementation Status
**Completed Features:**
- Path tracing with diffuse/specular/transmissive materials
- Color texture mapping
- HDR environment lighting (skybox)
- Anti-aliasing
- Hardware-accelerated BVH (via DXR)

**In Development:**
- Scene creation and optimization
- Advanced material layers (clearcoat, sheen, subsurface)
- Normal/height/attribute maps
- Importance sampling with Russian Roulette
- Point and area lights
- Simulation-based content

**Planned (Not Yet Started):**
- Principled BSDF
- Multi-layer materials
- Adaptive mipmap
- Volumetric rendering
- Motion blur, depth of field
- Advanced visual effects

## Architecture

### Core Components
- **Renderer (`Renderer.h/cpp`)**: DirectX 12 DXR pipeline managing acceleration structures, shader binding tables, and raytracing dispatch
- **Scene (`Scene.h/cpp`)**: Container for meshes, materials, lights with async loading via Python loaders
- **Material System (`Material.h`, `MaterialLayers.h`)**: 64-byte GPU-aligned PBR materials with infrastructure for layered extensions (implementation in progress)
- **Virtual Texture System (`VirtualTextureSystem.h/cpp`)**: Tile-based texture streaming infrastructure (256×256 tiles, not yet fully integrated)
- **Denoiser (`Denoiser.h/cpp`)**: Integration with Intel Open Image Denoise for progressive rendering cleanup

### Data Flow
1. **Scene Conversion** (Separate Step): GUI conversion tool calls Python loaders → binary `.acg` format
2. **Scene Loading**: Load pre-converted `.acg` files → C++ Scene object (no automatic conversion)
3. **GPU Upload**: Scene data converted to GPU structures → uploaded to default heaps via staging buffers
4. **Acceleration Structure**: Bottom-level AS per mesh, top-level AS for scene (built using `CreateAccelerationStructures()`)
5. **Raytracing**: HLSL shaders (`shaders/Raytracing.hlsl`) trace rays using acceleration structures
6. **Output**: Accumulation buffer → optional denoising → swapchain for display or PPM file export

### CPU-GPU Data Synchronization
- **GPUMaterial** (96 bytes, `Renderer.h`): Simplified structure for shader constant buffers
- **Material** struct (64 bytes, `Structures.hlsli`): GPU-side HLSL structure matching `MaterialData` in `Material.h`
- **Vertex** (48 bytes): Position, normal, texcoord, tangent (16-byte aligned)
- Material layer flags must match between `MaterialLayers.h` and `Structures.hlsli` defines

## Build System

### Dependencies (vcpkg)
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```
Required packages in `vcpkg.json`: glm, imgui (with dx12-binding), stb, d3dx12, openexr, tinyexr, miniz

### Python Scene Loader Setup
```bash
# From project root - creates .venv with Python 3.11 (required for bpy)
.\setup_loader.bat
```
Python dependencies in `requirements.txt`:
- **PyWavefront** (>=1.3.3): OBJ/MTL file parsing
- **bpy** (>=5.0.0): Blender Python API for .blend files
- **numpy** (>=1.21.0): Numerical operations
- **requests** (>=2.31.0): Test scene downloading

**Scene Conversion Workflow:**
- Conversion and loading are **completely separated** - no automatic conversion during loading
- GUI has two independent sections:
  1. **Scene Conversion**: Convert OBJ/FBX/GLTF/Blend → ACG binary (calls `bin/loader/main.py`)
  2. **Scene Loading**: Load pre-converted ACG files only (no conversion)
- Python loader uses executable directory path: `<exe_dir>/loader/main.py` (not working directory)
- Python output redirected to GUI log (ERROR level only, UTF-8 encoded)
- Conversion results displayed via ImGui modal popup

### Build Artifacts
- Executable: `bin/ACG_Project.exe`
- Shaders: Auto-copied from `shaders/` → `bin/shaders/`
- DLLs: OIDN, DXC compiler (from Windows SDK), PIX (Debug only)

## Development Conventions

### Shader Development
- **Always modify `.hlsl`/`.hlsli` files** in `shaders/`, not in `bin/shaders/` (auto-copied by CMake)
- Root signature is defined programmatically in `Renderer::CreateRaytracingRootSignature()`, NOT in shader code
- Shader compilation uses DXC at runtime via `D3DCompileFromFile()` (see `Renderer::CreateRaytracingPipeline()`)
- Use `#include` for shader includes: `#include "Structures.hlsli"` (relative paths)

### Material System Patterns
- Always initialize materials with `MaterialData()` constructor for safe defaults
- Current implementation supports basic PBR: baseColor, metallic, roughness, IOR, emission, opacity
- Layer system infrastructure exists but not fully implemented - exercise caution when using `MaterialLayers.h`
- Layer flags: Use bitwise OR to enable multiple layers (e.g., `LAYER_CLEARCOAT | LAYER_TRANSMISSION`) when implemented
- Extended layers stored in `Scene::m_materialLayers` vector, indexed via `extendedDataIndex` (infrastructure only)
- Texture indices: `-1` means no texture, `>= 0` indexes into texture array
- Virtual texture system has infrastructure but is not yet active in rendering pipeline

### Memory Alignment Rules
- GPU structures must be 16-byte aligned (use `alignas(16)`)
- vec3 in HLSL occupies 12 bytes but vec4 occupies 16 bytes - pad carefully
- Constant buffers must be 256-byte aligned (D3D12 requirement)
- Check `Structures.hlsli` comments for byte layouts

### Error Handling
- Use `ThrowIfFailed(hr, "context message")` for HRESULT checks (see `DX12Helper.h`)
- DirectX debug layer enabled in Debug builds - check Visual Studio Output pane for validation errors
- PIX for Windows integration (Debug only): Available for manual profiling, do NOT add `PIXBeginEvent()` / `PIXEndEvent()` in code unless explicitly requested

### Scene Loader Extension
To add new file format support:
1. Create loader class inheriting from `BaseLoader` in `loader/` (see `wavefront_loader.py`)
2. Register via `@LoaderRegistry.register` decorator with file extensions
3. Convert to `SceneData` structures in `data_structures.py`
4. Binary export handled automatically by `binary_exporter.py`
5. Use English-only messages in logger (Python outputs ERROR level only with UTF-8 encoding)

**Important**: `Scene::LoadFromFile()` only accepts `.acg` files and will reject other formats. All conversion must happen through GUI conversion tool first.

## Common Workflows

### Scene Conversion and Loading (Separated Architecture)

**Overview**: Scene conversion and rendering are completely separated to ensure clean workflow and avoid runtime conversion overhead.

**Step 1 - Scene Conversion** (GUI "Scene Conversion" section):
```
User selects source file (OBJ/FBX/GLTF/Blend)
→ Sets output ACG path
→ Clicks "Convert to ACG" button
→ GUI calls Python loader: <exe_dir>/loader/main.py
→ Python parses source → writes binary ACG
→ Modal popup shows success/failure
```

**Step 2 - Scene Loading** (GUI "Scene Loading" section):
```
User browses for ACG file (file dialog filtered to .acg only)
→ GUI validates extension is .acg
→ Renderer calls Scene::LoadFromFile()
→ Scene.cpp validates extension is .acg
→ Binary ACG loaded directly (no conversion)
→ Rendering begins
```

**Enforcement Points** (three-layer validation):
1. **GUI file browser**: Only shows `.acg` files
2. **GUI render thread**: Validates extension before starting render (lines 641-648)
3. **Scene::LoadFromFileEx**: Backend rejects non-ACG files (lines 53-59)

**Python Loader Details**:
- Path resolution: Uses `GetModuleFileNameW()` to find `<exe_dir>/loader/main.py`
- Virtual environment: Prefers `<exe_dir>/loader/.venv/Scripts/python.exe`, falls back to system Python
- Output encoding: Forces UTF-8 via `PYTHONIOENCODING=utf-8` environment variable
- Logging: ERROR level only, captured via `_popen()` and displayed in GUI log window
- Results: Modal popup shows success/failure, detailed logs in log window

**Key Implementation Files**:
- `src/GUI.cpp` lines 427-557: Conversion tool with Python subprocess
- `src/GUI.cpp` lines 1069-1088: Modal popup for conversion results
- `src/Scene.cpp` lines 43-60: ACG-only validation in LoadFromFileEx
- `loader/main.py`: Python entry point with UTF-8 and ERROR-only logging

### Adding New Material Properties
1. Update `MaterialData` in `Material.h` (mind 64-byte limit)
2. Update `Material` struct in `Structures.hlsli` with matching layout
3. If property exceeds 64 bytes, use layered system: add to `MaterialLayers.h` and `Structures.hlsli`
4. Update Python `data_structures.py` for loader support
5. Modify shader logic in `Raytracing.hlsl` (e.g., `EvaluateMaterial()`)

**Note**: Multi-layer material system has infrastructure in place but shader-side implementation is incomplete. Test thoroughly when adding new layers.

### Debugging Rendering Issues
1. Check Visual Studio Output pane for D3D12 validation layer warnings
2. Verify shader resource views in descriptor heap (check `CreateShaderResources()`)
3. Common issues: incorrect descriptor heap indexing, missing barriers, mismatched CPU/GPU struct layouts

**Manual Testing/Profiling** (programmer invoked, not automated):
- PIX for Windows GPU capture (Debug builds): Analyze dispatch, resource bindings
- Test scenes: Run `tests/download.py` and `tests/unzip.py`, then load via GUI
- Avoid adding debug/profiling code (PIX events, print statements) unless explicitly needed

### Performance Profiling
- Monitor `m_accumulatedSamples` for progressive rendering progress
- Check `Scene::LoadStats` for memory estimates
- Virtual Texture stats available via `VirtualTextureSystem::GetStatistics()` (when active)
- GPU timing: Use PIX or `ID3D12CommandQueue::GetTimestampFrequency()`

## Test Scenes
Located in `tests/scenes/` (manually download via `python tests/download.py`, extract via `python tests/unzip.py`):
- `CornellBox`: Basic diffuse/specular test
- `breakfast_room`: Texture mapping stress test
- `sponza`: Large architectural scene
- `bistro`: Final complexity benchmark
- Other scenes: `bedroom`, `fireplace_room`, `gallery`, `San_Miguel`, `lpshead`, `cloud`, `hair_cards_fbx`

**Loading Workflow**:
1. Convert scene using GUI "Scene Conversion" section (OBJ/FBX/GLTF/Blend → ACG)
2. Load converted `.acg` file using GUI "Scene Loading" section
3. Start rendering

**Note**: Testing is manual - do not add automated test code unless requested.

## Key File References
- Pipeline setup: `Renderer::OnInit()` → `InitPipeline()` → `CreateRaytracingPipeline()`
- Shader entry points: `RayGenShader`, `ClosestHitShader`, `MissShader` in `Raytracing.hlsl`
- Material evaluation: `EvaluateMaterial()` function in `Raytracing.hlsl`
- Virtual texture lookup: `SampleVirtualTexture()` in `Raytracing.hlsl`
- Scene binary format: `binary_exporter.py` (write) and `Scene::LoadFromFile()` (read)

## External Resources
- DirectX Raytracing Spec: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
- OIDN Documentation: https://www.openimagedenoise.org/documentation.html
- Course requirements: `docs/ACG_2025_Project_Announcement.pdf`
