#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#include <glm.hpp>
#include "Shader.h"
#include "loadTexture.h" 

class PBRMaterial
{
public:
    // ---------------------------------------------------------
    // 构造函数 1：传统散装贴图模式 (全贴图)
    // ---------------------------------------------------------
    PBRMaterial(loadTexture* diff, loadTexture* nor, loadTexture* disp, loadTexture* metal, loadTexture* rough, loadTexture* ao) 
        : my_diff(diff), my_nor(nor), my_disp(disp), my_metal(metal), my_rough(rough), my_ao(ao), 
            isPacked(false), uvTiling(1.0f, 1.0f), 
            useAlbedoMap(true), albedoValue(glm::vec3(1.0f)),
            useNormalMap(true),
            useMetalMap(true), metalValue(0.0f),
            useRoughnessMap(true), roughnessValue(0.5f),
            useAOMap(true), aoValue(1.0f) {}

    // ---------------------------------------------------------
    // 构造函数 2：金属度使用常量 float 代替贴图 (专治无金属度的 Fab 贴图)
    // ---------------------------------------------------------
    PBRMaterial(loadTexture* diff, loadTexture* nor, loadTexture* disp, float metalVal, loadTexture* rough, loadTexture* ao) 
        : my_diff(diff), my_nor(nor), my_disp(disp), my_metal(nullptr), my_rough(rough), my_ao(ao), 
            isPacked(false), uvTiling(1.0f, 1.0f), 
            useAlbedoMap(true), albedoValue(glm::vec3(1.0f)),
            useNormalMap(true),
            useMetalMap(false), metalValue(metalVal),
            useRoughnessMap(true), roughnessValue(0.5f),
            useAOMap(true), aoValue(1.0f) {}

    // ---------------------------------------------------------
    // 构造函数 3：现代 ORM 三合一贴图模式
    // ---------------------------------------------------------
    PBRMaterial(loadTexture* diff, loadTexture* nor, loadTexture* disp, loadTexture* orm) 
        : my_diff(diff), my_nor(nor), my_disp(disp), my_metal(orm), my_rough(orm), my_ao(orm), 
            isPacked(true), uvTiling(1.0f, 1.0f), 
            useAlbedoMap(true), albedoValue(glm::vec3(1.0f)),
            useNormalMap(true),
            useMetalMap(true), metalValue(0.0f),
            useRoughnessMap(true), roughnessValue(0.5f),
            useAOMap(true), aoValue(1.0f) {}

    // ---------------------------------------------------------
    // 🌟 构造函数 4：纯参数模式 (打造完美抛光金属、镜面专用！)
    // ---------------------------------------------------------
    PBRMaterial(glm::vec3 albedoCol, float metalVal, float roughVal, float aoVal = 1.0f) 
        : my_diff(nullptr), my_nor(nullptr), my_disp(nullptr), my_metal(nullptr), my_rough(nullptr), my_ao(nullptr), 
            isPacked(false), uvTiling(1.0f, 1.0f), 
            useAlbedoMap(false), albedoValue(albedoCol),
            useNormalMap(false), 
            useMetalMap(false), metalValue(metalVal),
            useRoughnessMap(false), roughnessValue(roughVal),
            useAOMap(false), aoValue(aoVal) {}

    bool isPacked; 
    glm::vec2 uvTiling; 

    // 状态开关与固定数值
    bool useAlbedoMap;
    glm::vec3 albedoValue;

    bool useNormalMap;

    bool useMetalMap;   
    float metalValue;   

    bool useRoughnessMap;
    float roughnessValue;

    bool useAOMap;
    float aoValue;

    void bind(const Shader& shader) const
    {
        // 贴图安全绑定
        if (useAlbedoMap && my_diff) my_diff->bind(0);
        if (useNormalMap && my_nor) my_nor->bind(1);
        if (my_disp) my_disp->bind(2);
        if (useMetalMap && my_metal) my_metal->bind(3); 

        if (!isPacked) {
            if (useRoughnessMap && my_rough) my_rough->bind(4);
            if (useAOMap && my_ao) my_ao->bind(5);
        }

        shader.setBool("usePackedMap", isPacked); 
        shader.setVec2("uvTiling", uvTiling); 

        // 将开关和数值传给 Shader
        shader.setBool("useAlbedoMap", useAlbedoMap);
        if (!useAlbedoMap) shader.setVec3("albedoValue", albedoValue);

        shader.setBool("useNormalMap", useNormalMap);

        shader.setBool("useMetalMap", useMetalMap);
        if (!useMetalMap) shader.setFloat("metalValue", metalValue);

        shader.setBool("useRoughnessMap", useRoughnessMap);
        if (!useRoughnessMap) shader.setFloat("roughnessValue", roughnessValue);

        shader.setBool("useAOMap", useAOMap);
        if (!useAOMap) shader.setFloat("aoValue", aoValue);
    }

private:
    loadTexture* my_diff;
    loadTexture* my_nor;
    loadTexture* my_disp;
    loadTexture* my_metal;
    loadTexture* my_rough;
    loadTexture* my_ao;
};

#endif