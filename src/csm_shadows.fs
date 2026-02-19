#version 330 core
layout ( location = 0 ) out vec4 FragColor;
layout ( location = 1 ) out vec4 BrightColor;

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
uniform sampler2D diffuseTexture;  // 颜色 (Albedo)
uniform sampler2D normalMap;       // 法线
uniform sampler2D depthMap;        // 高度/置换 (Displacement)
uniform sampler2D roughnessMap;    // 🆕 粗糙度
uniform sampler2D aoMap;           // 🆕 环境光遮蔽

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

// ✅ 泊松圆盘采样的预定义向量数组 (硬编码)
// 这些向量指向四面八方，且彼此距离比较均匀
vec3 sampleOffsetDirections[20] = vec3[]
(
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

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

    // if (layer == cascadeCount) 
    // {
    //     bias *= 0.5; // 仅对最远一层做一点点优化，或者直接删掉这行也可以
    // }

    if (layer > 0) 
    {
        bias *= (layer + 1.0) * 0.5; 
    }

    // if (layer == cascadeCount)
    //     bias *= 1 / (far_plane * 0.5);
    // else
    //     bias *= 1 / (cascadePlaneDistances[layer] * 0.5);

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
    
    // 🕵️‍♂️ 调试代码：如果你想看层级颜色，把下面注释解开
    // if (layer == 0) return -1.0; // 红色标记 (需在 main 处理)
    // if (layer == 1) return -2.0; // 绿色标记

    // -----------------------------------------------------------

    // vec3 fragToLight = fragPosWorld - lightPos;
    // float currentDepth = length(fragToLight);

    // float shadow = 0.0;
    // float bias = 0.15; // 稍微大一点的 bias
    // int samples = 20;  // 采样次数

    // float viewDistance = length(viewPos - fragPosWorld);sssws    

    // // ✅ 动态半径：距离越远，阴影越软
    // // / 25.0 是一个经验参数，你可以调整它来控制模糊程度
    // float diskRadius = (1.0 + (viewDistance / far_plane)) / 25.0;

    // for(int i = 0; i < samples; ++i)
    // {
    //     // 采样：原来的方向 + 泊松偏移 * 半径
    //     float closestDepth = texture(shadowMap, fragToLight + sampleOffsetDirections[i] * diskRadius).r;

    //     // 还原单位
    //     closestDepth *= far_plane;   

    //     if(currentDepth - bias > closestDepth)
    //         shadow += 1.0;
    // }

    // shadow /= float(samples);

    return shadow;
}

vec2 parallaxMapping(vec2 texCoords, vec3 viewDir, vec3 lightDir, out float parallaxShadow)
{
    // ✅ 简单方法：直接根据深度贴图计算视差偏移
    // float height = texture(depthMap, texCoords).r; // 从深度贴图中获取高度
    // vec2 p = viewDir.xy / viewDir.z * (height * height_scale); // 计算视差偏移
    // return texCoords - p; // 返回新的纹理坐标

    // ✅ 迭代法实现视差映射
    // const float numLayers = 10.0; // 迭代层数
    const float minLayers = 8.0;
    const float maxLayers = 32.0;
    float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir))); // 根据视角动态调整层数

    float LayerDepth = 1.0 / numLayers; // 每层的深度
    float currentLayerDepth = 0.0; // 当前层的深度

    // vec2 p = viewDir.xy / max(viewDir.z, 0.1) * height_scale; // 每层的纹理坐标偏移
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
    float parallaxShadow; // 输出变量
    vec2 texCoords = parallaxMapping(fs_in.TexCoords, viewDir_Tangent, lightDir_Tangent, parallaxShadow);

    // --- 采样纹理 ---
    vec3 normal = texture(normalMap, texCoords).rgb;
    normal = normal * 2.0 - 1.0; // 从 [0,1] 转换到 [-1,1]
    normal = normalize(fs_in.TBN * normal); // 转换到世界空间
    float roughness = texture(roughnessMap, texCoords).r;
    float ao = texture(aoMap, texCoords).r;

    vec3 color = texture(diffuseTexture, texCoords).rgb;

    // Ambient
    vec3 ambient = 0.05 * lightColor * color * ao;
    // Diffuse
    // vec3 lightDir = normalize(lightPos - fs_in.FragPos);
    vec3 lightDir = normalize(lightDir);
    float diff = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = diff * lightColor * color;
    // Specular
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    float spec = 0.0;
    float shininess = (1.0 - roughness) * 128.0;
    if(blinn)
    {
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    }
    else
    {
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess / 4.0);
    }
    vec3 specular = spec * lightColor * (1.0 - roughness);

    // Calculate shadow
    vec3 debugCascadeColor = vec3(0.0);
    float shadow = ShadowCalculation(fs_in.FragPos, normal, debugCascadeColor);
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular) * (1.0 - parallaxShadow));
    // vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular));

    if(DEBUG_CSM_LAYER) {
        // 混合光照结果和层级颜色
        FragColor = vec4(lighting * 0.5 + debugCascadeColor * 0.5, 1.0);
    } else {
        FragColor = vec4(lighting, 1.0);
    }

    float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = FragColor;
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}