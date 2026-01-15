#ifndef TEXTURE_H
#define TEXTURE_H

#include <glad/glad.h>
#include <iostream>
#include "stb_image.h"

class Texture {
public:
    unsigned int ID;
    int width, height, nrChannels;

    // 构造函数
    Texture(const char* path) {
        glGenTextures(1, &ID);

        // 图像加载设置：翻转Y轴
        stbi_set_flip_vertically_on_load(true);
        // 加载图片数据 (CPU端)
        unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);

        if (data) {
            // 根据通道数判断格式
            GLenum format;
            if (nrChannels == 1) format = GL_RED;       // .jpg 黑白图
            else if (nrChannels == 3) format = GL_RGB;  // .jpg 彩色图
            else if (nrChannels == 4) format = GL_RGBA; // .png 透明图

            glBindTexture(GL_TEXTURE_2D, ID);

            // 告诉 OpenGL：我们的图片像素是紧挨着的，不要假设每行按 4 字节对齐
            // 这能防止宽度不为 4 倍数的图片导致崩溃
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            // 发送数据到 GPU (最核心步骤)
            // 目标 | Mipmap层级 | 显卡存储格式 | 宽 | 高 | 历史遗留0 | 源数据格式 | 数据类型 | 数据指针
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

            // 自动生成多级渐远纹理
            glGenerateMipmap(GL_TEXTURE_2D);

            // 设置纹理参数 (环绕和过滤)
            // 环绕方式: 重复
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            // 过滤方式: 线性插值 (平滑)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            std::cout << "[Texture] Loaded successfully: " << path << std::endl;
        }
        else {
            std::cout << "[Texture] Failed to load texture: " << path << std::endl;
        }

        // 释放 CPU 端的临时数据
        stbi_image_free(data);
    }

    // 析构函数
    ~Texture() {
        // 如果 ID 是 0，说明资源已经被移动走了，不需要释放
        if (ID != 0) {
            glDeleteTextures(1, &ID);
        }
    }

    // 禁用拷贝构造 (防止 Double Free)
    Texture(const Texture&) = delete;

    // 禁用拷贝赋值
    Texture& operator=(const Texture&) = delete;

    // 移动构造 (转移所有权)
    Texture(Texture&& other) noexcept {
        // 窃取资源
        this->ID = other.ID;
        this->width = other.width;
        this->height = other.height;
        this->nrChannels = other.nrChannels;

        // 把原对象的 ID 置为 0，防止它析构时删除资源
        other.ID = 0;
    }

    // 移动赋值
    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            // 如果自己手里原本有资源，先释放
            if (this->ID != 0) {
                glDeleteTextures(1, &this->ID);
            }

            // 窃取资源
            this->ID = other.ID;
            this->width = other.width;
            this->height = other.height;
            this->nrChannels = other.nrChannels;

            // 置空原对象
            other.ID = 0;
        }
        return *this;
    }

    // 绑定纹理到指定槽位
    // slot 通常是 0 (GL_TEXTURE0), 1 (GL_TEXTURE1) 等
    void bind(unsigned int slot = 0) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, ID);
    }
};

#endif