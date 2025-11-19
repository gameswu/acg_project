#pragma once

#include <memory>
#include <vector>
#include <string>

namespace ACG {

class Denoiser {
public:
    Denoiser();
    ~Denoiser();

    // 初始化降噪器
    bool Initialize();

    // 对图像进行降噪
    // input: RGB float数组 (width * height * 3)
    // output: RGB float数组 (width * height * 3)
    // width, height: 图像尺寸
    // albedo: 可选的反照率图 (width * height * 3)，可为nullptr
    // normal: 可选的法线图 (width * height * 3)，可为nullptr
    bool Denoise(
        const float* input,
        float* output,
        int width,
        int height,
        const float* albedo = nullptr,
        const float* normal = nullptr
    );

    // 获取错误信息
    std::string GetError() const { return m_errorMessage; }

    // 检查是否已初始化
    bool IsInitialized() const { return m_initialized; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized;
    std::string m_errorMessage;
};

} // namespace ACG
