#include <d3d12.h>
#include <iostream>

// 模拟HLSL RadiancePayload
struct RadiancePayloadCPU {
    float radiance[3];      // 12
    float throughput[3];    // 12
    float nextOrigin[3];    // 12
    float nextDirection[3]; // 12
    unsigned int rngState;  // 4
    unsigned int terminated; // 4 (bool in HLSL is 4 bytes)
    float misWeight;        // 4
    float iorStack[4];      // 16
    unsigned int iorStackTop; // 4
};

int main() {
    std::cout << "RadiancePayload size: " << sizeof(RadiancePayloadCPU) << " bytes\n";
    std::cout << "Offset of radiance: " << offsetof(RadiancePayloadCPU, radiance) << "\n";
    std::cout << "Offset of rngState: " << offsetof(RadiancePayloadCPU, rngState) << "\n";
    std::cout << "Offset of misWeight: " << offsetof(RadiancePayloadCPU, misWeight) << "\n";
    std::cout << "Offset of iorStack: " << offsetof(RadiancePayloadCPU, iorStack) << "\n";
    std::cout << "Offset of iorStackTop: " << offsetof(RadiancePayloadCPU, iorStackTop) << "\n";
    return 0;
}
