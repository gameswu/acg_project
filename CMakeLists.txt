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

# 收集所有头文件
file(GLOB_RECURSE HEADER_FILES 
    "include/*.h"
)

# 生成可执行文件
add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES} ${HEADER_FILES})

# 查找 vcpkg 安装的依赖包
find_package(glm REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)

# 将依赖库链接到您的可执行文件
target_link_libraries(${PROJECT_NAME} PRIVATE 
    glm::glm
    imgui::imgui
    assimp::assimp
    nlohmann_json::nlohmann_json
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

# 复制 DXC 编译器 DLL 到 bin 目录
# DXC 位于 Windows SDK 中
set(WINDOWS_SDK_BIN_PATH "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
if(EXISTS "${WINDOWS_SDK_BIN_PATH}/dxcompiler.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINDOWS_SDK_BIN_PATH}/dxcompiler.dll"
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dxcompiler.dll
        COMMENT "Copying dxcompiler.dll from Windows SDK"
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WINDOWS_SDK_BIN_PATH}/dxil.dll"
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/dxil.dll
        COMMENT "Copying dxil.dll from Windows SDK"
    )
else()
    message(WARNING "DXC DLLs not found at ${WINDOWS_SDK_BIN_PATH}, you may need to copy them manually")
endif()

# 设置Visual Studio的源文件组织
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES})

# 输出一些信息
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Source files found: ${SRC_FILES}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
