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
    void Draw(const Shader& shader, const Camera& camera, float screenWidth, float screenHeight)
    {
        if (!cityPart1 || !cityPart2) return;

        // 1. 🌟 每帧实时生成摄像机视锥体 (Frustum)
        // 注意：这里的 near 和 far (0.1f, 1000.0f) 必须和你生成 Projection 矩阵时的参数一模一样！
        Frustum frustum = createFrustumFromCamera(camera, screenWidth / screenHeight, glm::radians(camera.Zoom), 0.1f, 1000.0f);

        // 构建一个全局的模型矩阵
        glm::mat4 globalModelMatrix = glm::mat4(1.0f);

        // 将统一的矩阵传给 Shader
        shader.setMat4("model", globalModelMatrix);
        shader.setMat3("NormalMatrix", glm::transpose(glm::inverse(glm::mat3(globalModelMatrix))));

        // 3. 准备统计数据
        unsigned int totalProcessed = 0;
        unsigned int totalSentToGPU = 0;

        // 🚀 一键渲染全城！
        // cityPart1 和 cityPart2 内部已经自带了相对坐标，所以它们会自动完美拼接！
        cityPart1->DrawPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);
        cityPart2->DrawPBR_Culled(shader, frustum, globalModelMatrix, totalSentToGPU, totalProcessed);

        // 5. 打印性能监控日志 (建议测试成功后加上 if 频率限制，避免刷屏卡顿)
        // 🌟 核心修复：防止除以 0 导致 NaN，并且添加“计数器”防止刷屏卡顿！
        if (totalProcessed > 0)
        {
            static int frameCounter = 0;
            // 每渲染 60 帧（大约1秒）才打印一次，保持控制台清爽
            if (frameCounter % 60 == 0)
            {
                float savedPercentage = (1.0f - (float)totalSentToGPU / totalProcessed) * 100.0f;
                std::cout << "[Frustum Culling] Total Mesh: " << totalProcessed 
                            << " | Rendered: " << totalSentToGPU 
                            << " | Saved: " << savedPercentage << "%" << std::endl;
            }
            frameCounter++;
        }
    }

private:
    // 只需要这两个指针，告别满屏的 material 和 props 变量
    Model* cityPart1;
    Model* cityPart2;
};

#endif