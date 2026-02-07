#ifndef SKYBOX_HELPER_H
#define SKYBOX_HELPER_H

#include <vector>
#include <string>
#include <filesystem> // C++17 标准库，用于遍历文件夹
#include <iostream>
#include <algorithm> // 用于 transform 转小写

namespace fs = std::filesystem;

class SkyboxHelper {
public:
    // 静态函数：传入文件夹路径，返回排序好的 6 张图片路径
    static std::vector<std::string> GetFacesInOrder(const std::string& directory) {
        std::vector<std::string> faces(6); // 预分配 6 个空位
        
        // 1. 检查目录是否存在
        if (!fs::exists(directory)) {
            std::cout << "[Skybox] Error: Directory not found: " << directory << std::endl;
            return faces;
        }

        // 2. 遍历文件夹中的所有文件
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file()) { // 只要文件，不要子文件夹
                std::string filePath = entry.path().string();
                std::string fileName = entry.path().filename().string();
                
                // 转小写，方便匹配 (无论是 Right.jpg 还是 right.JPG 都能识别)
                std::string lowerName = fileName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                // 3. 智能归位逻辑 (根据 OpenGL Cubemap 的标准顺序)
                // 索引 0: Right  (+X)
                if (lowerName.find("right") != std::string::npos || lowerName.find("posx") != std::string::npos) {
                    faces[0] = filePath;
                }
                // 索引 1: Left   (-X)
                else if (lowerName.find("left") != std::string::npos || lowerName.find("negx") != std::string::npos) {
                    faces[1] = filePath;
                }
                // 索引 2: Top    (+Y)
                else if (lowerName.find("top") != std::string::npos || lowerName.find("posy") != std::string::npos) {
                    faces[2] = filePath;
                }
                // 索引 3: Bottom (-Y)
                else if (lowerName.find("bottom") != std::string::npos || lowerName.find("negy") != std::string::npos) {
                    faces[3] = filePath;
                }
                // 索引 4: Back   (+Z)  <-- 注意！在 OpenGL 右手系中，+Z 是背后
                else if (lowerName.find("back") != std::string::npos || lowerName.find("posz") != std::string::npos) {
                    faces[4] = filePath;
                }
                // 索引 5: Front  (-Z)  <-- 注意！-Z 是前方
                else if (lowerName.find("front") != std::string::npos || lowerName.find("negz") != std::string::npos) {
                    faces[5] = filePath;
                }
            }
        }

        // 4. 安全检查：是不是 6 张都齐了？如果不齐，防止程序崩溃，用第一张填补空位
        for (int i = 0; i < 6; ++i) {
            if (faces[i].empty()) {
                std::cout << "[Skybox] Warning: Missing texture for index " << i << " in " << directory << std::endl;
                if(!faces[0].empty()) faces[i] = faces[0]; 
            }
        }

        return faces;
    }
};

#endif