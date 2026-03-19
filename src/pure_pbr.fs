#version 330 core
layout ( location = 0 ) out vec4 FragColor;

in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightDir;
    mat3 TBN;
}fs_in;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

layout (std140) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[16];
};

// --- 纹理全家桶 ---
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D depthMap;
uniform sampler2D metallicMap; // 如果是三合一，这其实是 ORM 贴图！
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;

// --- IBL 环境光贴图 ---
uniform samplerCube irradianceMap; // 🌟 预计算的漫反射辐照度贴图
uniform samplerCube prefilterMap;  // 🌟 预过滤的环境高光贴图
uniform sampler2D   brdfLUT;       // 🌟 BRDF 积分查找表

// --- CSM 阴影专用 ---
uniform sampler2DArray shadowMap;  // 🆕 这里的类型变了！是数组！
uniform float cascadePlaneDistances[16]; // CSM 切片距离 (C++传进来)
uniform int cascadeCount;          // 级联层数 (比如 5)

// --- 光照参数 (定向光) ---
// 注意：定向光不需要 lightPos，只需要方向 lightDir
uniform vec3 lightDir;             // 🌞 太阳光的方向 (指向光源)
uniform vec3 viewPos;
uniform vec3 lightColor;


// --- 其他配置 ---
uniform float height_scale;
uniform float far_plane;
uniform bool blinn;
const bool DEBUG_CSM_LAYER = false;
uniform bool useParallax; // 🌟 新增：视差贴图开关
uniform bool usePackedMap; // 三合一开关

const float PI = 3.14159265359;

float D_GGX_TR(vec3 N, vec3 H, float roughness)
{
    float a2 = roughness * roughness;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1;
    float k_dir = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k_dir) + k_dir;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 🌟 新增：带有粗糙度修正的 Fresnel 函数 (用于环境光)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowCalculation(vec3 fragPosWorld, vec3 normal, out vec3 debugColor)
{
    // 🆕 CSM 阴影计算核心
    vec4 fragPosViewSpace = view * vec4(fragPosWorld, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;
    for(int i = 0; i < cascadeCount; i++)
    {
        if(depthValue < cascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
        if(layer == -1)
        {
            layer = cascadeCount - 1;
        }
    }

    // --- 调试颜色输出 ---
    if(layer == 0) debugColor = vec3(1, 0, 0); // 近处：红
    else if(layer == 1) debugColor = vec3(0, 1, 0); // 中间：绿
    else if(layer == 2) debugColor = vec3(0, 0, 1); // 远处：蓝
    else debugColor = vec3(1, 1, 0); // 更远：黄
    // -------------------

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorld, 1.0);

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;
    if (currentDepth > 1.0) 
        return 0.0;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0)
    return 0.0;

    // 🚨 关键诊断逻辑 🚨
    // 检查是否超出阴影贴图范围 (Light Frustum Bounds)
    bool outOfBounds = (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0);
    
    if (outOfBounds)
    {
        // 如果这里返回 0.0 (无阴影)，但你没设置 Border Color 为白色
        // PCF 采样可能会采样到边缘内的黑色像素，导致 Shader 以为在阴影里
        // 这里我强制把越界区域显示为【白色】，帮你确认是否是矩阵太小的问题
        if(DEBUG_CSM_LAYER) debugColor = vec3(5.0, 5.0, 5.0); // 超亮白
        return 0.0;
    }

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);

    if (layer > 0) 
    {
        bias *= (layer + 1.0) * 0.5; 
    }

    // PCF 采样 (3x3)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            // ⚠️ 关键：采样 sampler2DArray 时使用 vec3(x, y, layer)
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, layer)).r; 
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    return shadow;
}

vec2 parallaxMapping(vec2 texCoords, vec3 viewDir, vec3 lightDir, out float parallaxShadow)
{
    // ✅ 迭代法实现视差映射
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir))); // 根据视角动态调整层数

    float LayerDepth = 1.0 / numLayers; // 每层的深度
    float currentLayerDepth = 0.0; // 当前层的深度

    vec2 p = viewDir.xy / max(viewDir.z, 0.1); // 先算标准偏移
    p = p * height_scale;                      // 乘上高度缩放

    // 🔥 关键防暴走逻辑：如果偏移量太长，强制截断 🔥
    float maxOffset = 0.05; // 限制最大偏移量 (根据实际效果微调，0.05-0.1 比较合适)
    if (length(p) > maxOffset)
    {
        p = normalize(p) * maxOffset;
    }

    vec2 deltaTexCoords = p / numLayers; // 每层的纹理坐标增量

    vec2 currentTexCoords = texCoords; // 当前层的纹理坐标
    float currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 当前层的深度贴图值

    // while(currentLayerDepth < currentDepthMapValue)
    // {
    //     currentTexCoords -= deltaTexCoords; // 移动到下一层
    //     currentLayerDepth += LayerDepth; // 增加当前层的深度
    //     currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 获取新层的深度贴图值
    // }
    // 🛡️ 安全锁：使用 for 循环代替 while，防止显卡未响应
    for(int i = 0; i < 40; ++i) 
    {
        if(currentLayerDepth >= currentDepthMapValue) 
            break; // 找到深度层了，退出

        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(depthMap, currentTexCoords).r;  
        currentLayerDepth += LayerDepth;  
    }
    // return currentTexCoords; // 返回最终的纹理坐标

    vec2 prevTexCoords = currentTexCoords + deltaTexCoords; // 上一层的纹理坐标

    float afterDepth = currentDepthMapValue - currentLayerDepth; // 当前层的深度差
    float beforeDepth = texture(depthMap, prevTexCoords).r - (currentLayerDepth - LayerDepth); // 上一层的深度差

    float weight = afterDepth / (afterDepth - beforeDepth); // 线性插值权重

    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight); // 线性插值计算最终纹理坐标

    // return finalTexCoords; // 返回最终的纹理坐标

    // ✅ 新增 - 视差自阴影 (找光线遮挡)
    vec2 P_Light = lightDir.xy / max(lightDir.z, 0.1) * height_scale; // 光线方向的纹理坐标偏移
    vec2 deltaTexCoordsLight = P_Light / numLayers; // 每层的光线纹理坐标增量

    float currentLayerDepthLight = texture(depthMap, finalTexCoords).r; // 最终层的深度贴图值
    vec2 currentTexCoordsLight = finalTexCoords; // 最终层的纹理坐标

    float shadowAccumulation = 0.0; // 积累阴影
    int shadowSamples = 0; // 采样计数

    // while(currentLayerDepthLight > 0.0)
    // {
    //     currentTexCoordsLight += deltaTexCoordsLight; // 沿光线方向移动
    //     currentLayerDepthLight -= LayerDepth; // 增加当前层的深度

    //     float currentDepthMapValueLight = texture(depthMap, currentTexCoordsLight).r; // 获取新层的深度贴图值

    //     // 判定：如果【光线现在的深度】 > 【当前地形深度】
    //     // 说明光线还在地形下面（被挡住了！）
    //     if(currentLayerDepthLight > currentDepthMapValueLight)
    //     {
    //         // 被挡住了！累加阴影
    //         // 柔和阴影：累加权重
    //         shadowAccumulation += 1.0;
    //     }
    //     shadowSamples++;
    // }
    // 🛡️ 安全锁：for 循环
        for(int i = 0; i < 40; ++i)
        {
            // 往上走，直到走出表面 (depth <= 0)
            if(currentLayerDepthLight <= 0.0) 
                break;

            currentLayerDepthLight -= LayerDepth;
            currentTexCoordsLight += deltaTexCoordsLight;
            float currentDepthMapValueLight = texture(depthMap, currentTexCoordsLight).r;

            // 如果现在的光线高度 < 地形高度，说明被挡住了
            if(currentLayerDepthLight > currentDepthMapValueLight)
            {
                // 简单的软阴影累加
                shadowAccumulation += 1.0; 
            }
            shadowSamples++;
        }

    // 计算最终阴影系数 (0.0 = 全黑, 1.0 = 全亮)
    // 如果没有样本被挡住，结果是 1.0
    // 如果有一半被挡住，结果是 0.5 (模拟软阴影)
    if(shadowSamples > 0)
        parallaxShadow = shadowAccumulation / float(shadowSamples); // 计算平均阴影
    else
        parallaxShadow = 0.0;

    return finalTexCoords; // 返回最终的纹理坐标
}

void main()
{
    // --- 准备切线空间向量 ---
    vec3 viewDir_Tangent = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);
    // vec3 lightDir_Tangent = normalize(fs_in.TangentLightPos - fs_in.TangentFragPos);
    vec3 lightDir_Tangent = normalize(fs_in.TangentLightDir);

    // --- 视差映射 ---
    // 🌟 核心修改：动态选择是否使用视差贴图
    vec2 texCoords = fs_in.TexCoords;
    float parallaxShadow = 0.0;
    
    if (useParallax) {
        texCoords = parallaxMapping(fs_in.TexCoords, viewDir_Tangent, lightDir_Tangent, parallaxShadow);
    }

    // --- 采样纹理 ---
    vec3 albedo = texture(albedoMap, texCoords).rgb;
    vec3 normal = texture(normalMap, texCoords).rgb;

    float metallic;
    float roughness;
    float ao;

    if (usePackedMap) 
    {
        vec3 orm = texture(metallicMap, texCoords).rgb;
        // 按照业界标准 (glTF/Unreal) 解析 ORM：
        // R 通道 = Ambient Occlusion (AO)
        // G 通道 = Roughness
        // B 通道 = Metallic
        ao        = orm.r;
        roughness = orm.g;
        metallic  = orm.b;
    } 
    else 
    {
        // 传统散装模式：从各自的 sampler 里提取 R 通道
        metallic  = texture(metallicMap, texCoords).r;
        roughness = texture(roughnessMap, texCoords).r;
        ao        = texture(aoMap, texCoords).r;
    }

    normal = normal * 2.0 - 1.0; // 从 [0,1] 转换到 [-1,1]
    normal = normalize(fs_in.TBN * normal); // 转换到世界空间
    vec3 N = normalize(normal);

    vec3 V = normalize(viewPos - fs_in.FragPos);
    vec3 R = reflect(-V, N); // 🌟 计算反射向量 (IBL 采样预过滤贴图时需要)

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 直接光照
    vec3 Lo = vec3(0.0);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);

    float attenuation = 1.0;
    vec3 radiance = lightColor * attenuation;

    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = D_GGX_TR(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = nominator / denominator;

    // 能量守恒：漫反射与镜面反射互斥
    vec3 KS = F;
    vec3 KD = vec3(1.0) - KS;
    KD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    Lo += (KD * albedo / PI + specular) * radiance * NdotL;

    // 间接光照计算 (IBL 环境光)
    vec3 KS_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 KD_ambient = 1.0 - KS_ambient;
    KD_ambient *= 1.0 - metallic; // // 同样，纯金属的漫反射为 0

    // 漫反射 IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse_ambient = irradiance * albedo;

    // 镜面反射 IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specular_ambient = prefilteredColor * (KS_ambient * envBRDF.x + envBRDF.y);

    vec3 ambient = (KD_ambient * diffuse_ambient + specular_ambient) * ao;

    // Calculate shadow
    vec3 debugCascadeColor = vec3(0.0);
    float shadow = ShadowCalculation(fs_in.FragPos, normal, debugCascadeColor);
    float visibility = (1.0 - shadow) * (1.0 - parallaxShadow);

    vec3 color = ambient + Lo * visibility;

    if(DEBUG_CSM_LAYER) {
        // 混合光照结果和层级颜色
        FragColor = vec4(color * 0.5 + debugCascadeColor * 0.5, 1.0);
    } else {
        FragColor = vec4(color, 1.0);
    }
}