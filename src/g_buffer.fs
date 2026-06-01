#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gAlbedo_parallaxShadow;
layout (location = 3) out vec4 gORM; // 借用 Alpha 通道传递水坑遮罩！

in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightDir;
    mat3 TBN;
}fs_in;

// --- 纹理全家桶 ---
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D depthMap;
uniform sampler2D metallicMap; // 如果是三合一，这其实是 ORM 贴图！
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;

// 其他配置
uniform float height_scale;
uniform bool useParallax;
uniform bool usePackedMap;

// --- 🌟 材质状态开关与纯数字接收 ---
uniform bool useAlbedoMap;
uniform vec3 albedoValue;

uniform bool useNormalMap;

uniform bool useMetalMap;    
uniform float metalValue;    

uniform bool useRoughnessMap;
uniform float roughnessValue;

uniform bool useAOMap;
uniform float aoValue;

// 🌧️ [新增] 全局湿度控制与噪音贴图，从光照阶段搬移到这里
uniform float u_GlobalWetness;
uniform sampler2D puddleNoiseMap;

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
    vec2 texCoords = fs_in.TexCoords;

    vec3 viewDir_Tangent = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);
    vec3 lightDir_Tangent = normalize(fs_in.TangentLightDir);

    float parallaxShadow = 0.0;
    
    if (useParallax)
    {
        texCoords = parallaxMapping(texCoords, viewDir_Tangent, lightDir_Tangent, parallaxShadow);
    }

    // ==========================================
    // 🌟 全面接管：根据开关决定是读贴图还是用纯数字
    // ==========================================

    // 1. 获取反照率 (Albedo)
    vec3 albedo = useAlbedoMap ? texture(albedoMap, texCoords).rgb : albedoValue;

    // 2. 获取法线 (Normal)
    vec3 normal;
    if (useNormalMap) 
    {
        normal = texture(normalMap, texCoords).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(fs_in.TBN * normal); 
    } 
    else 
    {
        // 💡 魔法：如果没有法线贴图（比如完美金属球），直接使用极其平滑的几何法线 (TBN 矩阵的 Z 轴)
        normal = normalize(fs_in.TBN[2]);
    }

    // 3. 获取 PBR 三大金刚 (ORM)
    float metallic, roughness, ao;
    if (usePackedMap)
    {
        vec3 orm = texture(metallicMap, texCoords).rgb;
        ao        = orm.r;
        roughness = orm.g;
        metallic  = orm.b;
    }
    else
    {
        metallic  = useMetalMap     ? texture(metallicMap, texCoords).r  : metalValue;
        roughness = useRoughnessMap ? texture(roughnessMap, texCoords).r : roughnessValue;
        ao        = useAOMap        ? texture(aoMap, texCoords).r        : aoValue;
    }

    // ==========================================
    // 🌧️ 工业级程序化水坑系统 (Organic Procedural Puddles)
    // ==========================================
    vec3 geoNormal = normalize(fs_in.TBN[2]);
    // 严格限制：只在绝对平坦的地面生成水坑 (Y > 0.9)
    float isGround = smoothstep(0.9, 0.95, geoNormal.y);

    if (isGround > 0.0) 
    {
        // 💡 魔法升级：分形正弦噪声 (Fractal Sine Noise)
        // 叠加三个不同频率的波，模拟大水坑里面套小水坑的自然边缘
        vec2 pos = fs_in.FragPos.xz;
        float noise = sin(pos.x * 0.4) * cos(pos.y * 0.4) * 0.5 + 0.5; // 大轮廓
        noise += sin(pos.x * 1.5 + 1.0) * cos(pos.y * 1.2 - 0.5) * 0.25; // 中细节
        noise += sin(pos.x * 3.0 + 2.0) * cos(pos.y * 3.0 + 1.0) * 0.125; // 小碎边
        
        // 将 noise 严格映射回 0.0 ~ 1.0 之间
        noise = clamp(noise, 0.0, 1.0);

        // 🌟 解决“反过来”的核心：反转遮罩逻辑！
        // 让 noise 的“低洼处”变成水坑。
        // u_GlobalWetness 控制水面高度。假设设为 0.3，说明只淹没底层 30% 的低洼区域。
        float puddleMask = 1.0 - smoothstep(u_GlobalWetness - 0.05, u_GlobalWetness + 0.05, noise);
        
        puddleMask *= isGround; // 确保水坑只在地面上

        // 强行覆盖 PBR 属性，变身水面！
        albedo = mix(albedo, albedo * 0.3, puddleMask);      // 水坑底色变暗，吸收光线
        
        // 真实的街道水坑边缘有一圈半湿润的过渡带，中心才是绝对光滑 (0.02)
        roughness = mix(roughness, 0.02, puddleMask);        
        metallic = mix(metallic, 0.0, puddleMask);           // 水绝对不是金属
        
        vec3 flatNormal = geoNormal; 
        // 抹平法线：积水深的地方像镜子一样平整，水浅的地方透出一点柏油路的粗糙
        normal = normalize(mix(normal, flatNormal, puddleMask * 0.9)); 
        
        gORM = vec4(ao, roughness, metallic, puddleMask);    // 把水坑 Mask 存入 Alpha 通道供 SSR 使用
    }
    else 
    {
        gORM = vec4(ao, roughness, metallic, 0.0);
    }

    // 输出至 G-Buffer
    gPosition = fs_in.FragPos;
    gNormal = normal; 
    gAlbedo_parallaxShadow = vec4(albedo, parallaxShadow);
}