#ifndef RAINY_ALLEY_SCENE_H
#define RAINY_ALLEY_SCENE_H

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include "Shader.h"
#include "Model.h"

class RainyAlleyScene
{
public:
    RainyAlleyScene() : cityPart1(nullptr), cityPart2(nullptr) {}

    ~RainyAlleyScene() {
        if (cityPart1) delete cityPart1;
        if (cityPart2) delete cityPart2;
    }

    void Init()
    {
        // ==========================================
        // 🚀 工业级加载：一行代码顶过去几百行手工搭建
        // ==========================================

        // Assimp 会自动解析里面成百上千个建筑、空调、垃圾桶的坐标和父子层级！
        cityPart1 = new Model("../extern/UE_Source/RainyStreet_1/Neon_Streets.gltf");
        cityPart2 = new Model("../extern/UE_Source/RainyStreet_2/Neon_Streets.gltf");

        std::cout << "Part 1 Mesh 数量: " << cityPart1->meshes.size() << std::endl;
        std::cout << "Part 2 Mesh 数量: " << cityPart2->meshes.size() << std::endl;
    }

    // ==========================================
    // 🎬 渲染主场景：极简调用
    // ==========================================
    void Draw(const Shader& shader)
    {
        if (!cityPart1 || !cityPart2) return;

        // 构建一个全局的模型矩阵
        glm::mat4 globalModelMatrix = glm::mat4(1.0f);

        // 将统一的矩阵传给 Shader
        shader.setMat4("model", globalModelMatrix);
        shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(globalModelMatrix))));

        // 🚀 一键渲染全城！
        // cityPart1 和 cityPart2 内部已经自带了相对坐标，所以它们会自动完美拼接！
        cityPart1->DrawPBR(shader);
        cityPart2->DrawPBR(shader);
    }

private:
    // 只需要这两个指针，告别满屏的 material 和 props 变量
    Model* cityPart1;
    Model* cityPart2;
};

#endif