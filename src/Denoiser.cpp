#include "Denoiser.h"
#include <OpenImageDenoise/oidn.hpp>
#include <iostream>

namespace ACG {

class Denoiser::Impl {
public:
    oidn::DeviceRef device;
    oidn::FilterRef filter;
};

Denoiser::Denoiser() 
    : m_impl(std::make_unique<Impl>())
    , m_initialized(false)
{
}

Denoiser::~Denoiser() {
    if (m_impl && m_impl->filter) {
        m_impl->filter = nullptr;
    }
    if (m_impl && m_impl->device) {
        m_impl->device = nullptr;
    }
}

bool Denoiser::Initialize() {
    try {
        // 创建OIDN设备（自动选择最佳设备）
        m_impl->device = oidn::newDevice();
        m_impl->device.commit();

        // 检查设备是否成功创建
        const char* errorMessage;
        if (m_impl->device.getError(errorMessage) != oidn::Error::None) {
            m_errorMessage = std::string("OIDN device creation failed: ") + errorMessage;
            std::cerr << m_errorMessage << std::endl;
            return false;
        }

        std::cout << "OIDN initialized successfully" << std::endl;
        m_initialized = true;
        return true;

    } catch (const std::exception& e) {
        m_errorMessage = std::string("OIDN initialization exception: ") + e.what();
        std::cerr << m_errorMessage << std::endl;
        return false;
    }
}

bool Denoiser::Denoise(
    const float* input,
    float* output,
    int width,
    int height,
    const float* albedo,
    const float* normal
) {
    if (!m_initialized) {
        m_errorMessage = "Denoiser not initialized";
        return false;
    }

    if (!input || !output || width <= 0 || height <= 0) {
        m_errorMessage = "Invalid input parameters";
        return false;
    }

    try {
        // 创建降噪过滤器
        m_impl->filter = m_impl->device.newFilter("RT");

        // 设置输入图像
        m_impl->filter.setImage("color", const_cast<float*>(input), 
                                oidn::Format::Float3, width, height);
        
        // 设置输出图像
        m_impl->filter.setImage("output", output, 
                                oidn::Format::Float3, width, height);

        // 如果提供了albedo和normal，使用它们提高降噪质量
        if (albedo) {
            m_impl->filter.setImage("albedo", const_cast<float*>(albedo),
                                    oidn::Format::Float3, width, height);
        }

        if (normal) {
            m_impl->filter.setImage("normal", const_cast<float*>(normal),
                                    oidn::Format::Float3, width, height);
        }

        // 设置HDR模式（对于路径追踪渲染）
        m_impl->filter.set("hdr", true);

        // 提交过滤器设置
        m_impl->filter.commit();

        // 执行降噪
        m_impl->filter.execute();

        // 检查错误
        const char* errorMessage;
        if (m_impl->device.getError(errorMessage) != oidn::Error::None) {
            m_errorMessage = std::string("OIDN denoising failed: ") + errorMessage;
            std::cerr << m_errorMessage << std::endl;
            return false;
        }

        std::cout << "Image denoised successfully (" << width << "x" << height << ")" << std::endl;
        return true;

    } catch (const std::exception& e) {
        m_errorMessage = std::string("OIDN denoising exception: ") + e.what();
        std::cerr << m_errorMessage << std::endl;
        return false;
    }
}

} // namespace ACG
