#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace ACG {

bool PersistentConfig::Load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "No config file found, using defaults" << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string key;
        if (!(iss >> key)) continue;
        
        if (key == "lastModelPath") {
            std::getline(iss >> std::ws, lastModelPath);
        }
        else if (key == "lastEnvMapPath") {
            std::getline(iss >> std::ws, lastEnvMapPath);
        }
        else if (key == "cameraPosition") {
            iss >> cameraPosition.x >> cameraPosition.y >> cameraPosition.z;
        }
        else if (key == "cameraDirection") {
            iss >> cameraDirection.x >> cameraDirection.y >> cameraDirection.z;
        }
        else if (key == "cameraFOV") {
            iss >> cameraFOV;
        }
        else if (key == "samplesPerPixel") {
            iss >> samplesPerPixel;
        }
        else if (key == "maxBounces") {
            iss >> maxBounces;
        }
        else if (key == "envLightIntensity") {
            iss >> envLightIntensity;
        }
        else if (key == "vtTileBatchSize") {
            iss >> vtTileBatchSize;
        }
        else if (key == "renderBatchSize") {
            iss >> renderBatchSize;
        }
        else if (key == "sunIntensity") {
            iss >> sunIntensity;
        }
        else if (key == "sunAzimuth") {
            iss >> sunAzimuth;
        }
        else if (key == "sunElevation") {
            iss >> sunElevation;
        }
        else if (key == "sunColor") {
            iss >> sunColor.r >> sunColor.g >> sunColor.b;
        }
        else if (key == "outputWidth") {
            iss >> outputWidth;
        }
        else if (key == "outputHeight") {
            iss >> outputHeight;
        }
        else if (key == "lastOutputPath") {
            std::getline(iss >> std::ws, lastOutputPath);
        }
    }
    
    file.close();
    std::cout << "Config loaded from: " << filename << std::endl;
    return true;
}

bool PersistentConfig::Save(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to save config to: " << filename << std::endl;
        return false;
    }
    
    file << "# ACG Renderer Configuration\n";
    file << "# Last saved: " << __DATE__ << " " << __TIME__ << "\n\n";
    
    file << "# Scene paths\n";
    file << "lastModelPath " << lastModelPath << "\n";
    file << "lastEnvMapPath " << lastEnvMapPath << "\n\n";
    
    file << "# Camera settings\n";
    file << "cameraPosition " << cameraPosition.x << " " << cameraPosition.y << " " << cameraPosition.z << "\n";
    file << "cameraDirection " << cameraDirection.x << " " << cameraDirection.y << " " << cameraDirection.z << "\n";
    file << "cameraFOV " << cameraFOV << "\n\n";
    
    file << "# Render Settings\n";
    file << "samplesPerPixel " << samplesPerPixel << "\n";
    file << "maxBounces " << maxBounces << "\n";
    file << "envLightIntensity " << envLightIntensity << "\n";
    file << "vtTileBatchSize " << vtTileBatchSize << "  # Virtual Texture tiles per GPU batch (10-200, default: 50)\n";
    file << "renderBatchSize " << renderBatchSize << "  # Samples per GPU execution (1-20, default: 5)\n\n";
    
    file << "# Sun settings\n";
    file << "sunIntensity " << sunIntensity << "\n";
    file << "sunAzimuth " << sunAzimuth << "\n";
    file << "sunElevation " << sunElevation << "\n";
    file << "sunColor " << sunColor.r << " " << sunColor.g << " " << sunColor.b << "\n\n";
    
    file << "# Output settings\n";
    file << "outputWidth " << outputWidth << "\n";
    file << "outputHeight " << outputHeight << "\n";
    file << "lastOutputPath " << lastOutputPath << "\n";
    
    file.close();
    std::cout << "Config saved to: " << filename << std::endl;
    return true;
}

} // namespace ACG
