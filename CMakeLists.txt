cmake_minimum_required(VERSION 3.10)

project(ACG_Project CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# 添加头文件目录
include_directories(include)

# Intel Open Image Denoise (OIDN) support
set(OIDN_ROOT ${CMAKE_SOURCE_DIR}/lib/oidn)
include_directories(${OIDN_ROOT}/include)
link_directories(${OIDN_ROOT}/lib)
message(STATUS "OIDN enabled: ${OIDN_ROOT}")

# PIX for Windows support (DEBUG only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include_directories(${CMAKE_SOURCE_DIR}/lib/WinPixEventRuntime/Include)
    message(STATUS "PIX enabled for Debug build")
else()
    message(STATUS "PIX disabled for Release build")
endif()

# 收集所有源文件
file(GLOB_RECURSE SRC_FILES 
    "src/*.cpp"
)

# Ensure VirtualTextureSystem.cpp is included
list(APPEND SRC_FILES "${CMAKE_SOURCE_DIR}/src/VirtualTextureSystem.cpp")

# 收集所有头文件
file(GLOB_RECURSE HEADER_FILES 
    "include/*.h"
)

# 添加资源文件
set(RESOURCE_FILES
    "${CMAKE_SOURCE_DIR}/ACG_Project.rc"
    "${CMAKE_SOURCE_DIR}/include/resource.h"
)

# 生成可执行文件
add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES} ${HEADER_FILES} ${RESOURCE_FILES})

# 查找 vcpkg 安装的依赖包
find_package(glm REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

# 将依赖库链接到您的可执行文件
target_link_libraries(${PROJECT_NAME} PRIVATE 
    glm::glm
    imgui::imgui
    OpenEXR::OpenEXR
    unofficial::tinyexr::tinyexr
    miniz::miniz
)

# stb 是仅头文件库，只需要包含目录
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})

# 链接 Windows SDK 提供的 DirectX 库
# 这些库由 Windows SDK 自动提供，无需通过 vcpkg 安装
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d12.lib      # Direct3D 12
    dxgi.lib       # DirectX Graphics Infrastructure
    d3dcompiler.lib # 着色器编译器
    dxguid.lib     # DirectX GUIDs
)

# 链接 Intel OIDN 库
target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenImageDenoise.lib
)

# 链接 PIX 调试库 (DEBUG only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/WinPixEventRuntime/bin/x64/WinPixEventRuntime.lib
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_PIX=1)
endif()

# 复制 PIX DLL 到输出目录 (DEBUG only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/lib/WinPixEventRuntime/bin/x64/WinPixEventRuntime.dll
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/WinPixEventRuntime.dll
        COMMENT "Copying WinPixEventRuntime.dll (Debug)"
    )
endif()

# 复制 OIDN DLL 到输出目录
set(OIDN_DLLS
    OpenImageDenoise.dll
    OpenImageDenoise_core.dll
    OpenImageDenoise_device_cpu.dll
    tbb12.dll
)
foreach(OIDN_DLL ${OIDN_DLLS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OIDN_ROOT}/bin/${OIDN_DLL}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${OIDN_DLL}
        COMMENT "Copying ${OIDN_DLL}"
    )
endforeach()

# 复制 shader 文件到 bin 目录
file(GLOB SHADER_FILES "${CMAKE_SOURCE_DIR}/shaders/*.hlsl" "${CMAKE_SOURCE_DIR}/shaders/*.hlsli")
foreach(SHADER_FILE ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_FILE}
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${SHADER_NAME}
        COMMENT "Copying shader: ${SHADER_NAME}"
    )
endforeach()

# 创建 shaders 目录
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
)

# ============================================================================
# Python Loader Setup (Direct Script Approach)
# ============================================================================

message(STATUS "Copying Python loader to bin/loader/")

# Create loader directory
add_custom_command(TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/loader
    COMMENT "Creating loader directory"
)

# Copy entire loader directory to bin
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/loader
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/loader
    COMMENT "Copying loader directory"
)

# Copy setup script to bin
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/setup_loader.bat
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/setup_loader.bat
    COMMENT "Copying setup_loader.bat"
)

# Copy requirements.txt to bin
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/requirements.txt
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/loader/requirements.txt
    COMMENT "Copying requirements.txt"
)

message(STATUS "Python loader will use virtual environment at bin/loader/.venv")
message(STATUS "Run setup_loader.bat in project root to setup environment first")


# ============================================================================
# Copy DXC Compiler DLLs from Windows SDK
# ============================================================================

# Find Windows SDK installation directory
set(WINDOWS_SDK_BASE "C:/Program Files (x86)/Windows Kits/10/bin")

if(EXISTS ${WINDOWS_SDK_BASE})
    # Get all SDK version directories and sort them
    file(GLOB SDK_VERSIONS "${WINDOWS_SDK_BASE}/10.0.*")
    list(SORT SDK_VERSIONS)
    list(REVERSE SDK_VERSIONS)  # Latest version first
    
    # Try each version until we find DXC DLLs
    set(DXC_FOUND FALSE)
    foreach(SDK_VERSION_PATH ${SDK_VERSIONS})
        get_filename_component(SDK_VERSION ${SDK_VERSION_PATH} NAME)
        set(WINDOWS_SDK_BIN_PATH "${SDK_VERSION_PATH}/x64")
        
        if(EXISTS "${WINDOWS_SDK_BIN_PATH}/dxcompiler.dll" AND EXISTS "${WINDOWS_SDK_BIN_PATH}/dxil.dll")
            message(STATUS "Found Windows SDK ${SDK_VERSION}")
            message(STATUS "DXC DLLs location: ${WINDOWS_SDK_BIN_PATH}")
            
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WINDOWS_SDK_BIN_PATH}/dxcompiler.dll"
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dxcompiler.dll
                COMMENT "Copying dxcompiler.dll from Windows SDK ${SDK_VERSION}"
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${WINDOWS_SDK_BIN_PATH}/dxil.dll"
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dxil.dll
                COMMENT "Copying dxil.dll from Windows SDK ${SDK_VERSION}"
            )
            
            set(DXC_FOUND TRUE)
            break()
        endif()
    endforeach()
    
    if(NOT DXC_FOUND)
        message(WARNING "DXC DLLs not found in any Windows SDK version at ${WINDOWS_SDK_BASE}")
        message(WARNING "You may need to install Windows SDK or copy DXC DLLs manually")
    endif()
else()
    message(WARNING "Windows SDK not found at ${WINDOWS_SDK_BASE}")
    message(WARNING "You may need to copy dxcompiler.dll and dxil.dll manually to bin/")
endif()

# 设置Visual Studio的源文件组织
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES})

# 输出一些信息
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Source files found: ${SRC_FILES}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
