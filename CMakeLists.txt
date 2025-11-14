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

# 收集所有源文件
file(GLOB_RECURSE SRC_FILES 
    "src/*.cpp"
)

# 收集所有头文件
file(GLOB_RECURSE HEADER_FILES 
    "include/*.h"
)

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})

# 查找 vcpkg 安装的依赖包
find_package(SDL2 CONFIG REQUIRED)
find_package(glm REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# 将依赖库链接到您的可执行文件
target_link_libraries(${PROJECT_NAME} PRIVATE 
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    glm::glm
    imgui::imgui
    assimp::assimp
    nlohmann_json::nlohmann_json
)

# stb 是仅头文件库，只需要包含目录
target_include_directories(${PROJECT_NAME} PRIVATE ${Stb_INCLUDE_DIR})

# 链接 Windows SDK 提供的 DirectX 库
# 这些库由 Windows SDK 自动提供，无需通过 vcpkg 安装
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11.lib      # Direct3D 11
    dxgi.lib       # DirectX Graphics Infrastructure
    d3dcompiler.lib # 着色器编译器
    dxguid.lib     # DirectX GUIDs
)

# 设置Visual Studio的源文件组织
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SRC_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES})

# 输出一些信息
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Source files found: ${SRC_FILES}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
