#version 330 core
layout ( location = 0 ) out vec4 FragColor;
layout ( location = 1 ) out vec4 BrightColor;

in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightPos;
    mat3 TBN;
}fs_in;

uniform sampler2D diffuseTexture;
uniform samplerCube shadowMap;
uniform sampler2D normalMap;
uniform sampler2D depthMap;

uniform vec3 lightPos;
uniform vec3 viewPos;

uniform vec3 lightColor;

uniform float height_scale;

uniform float far_plane;

uniform bool blinn;

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

float ShadowCalculation(vec3 FragPos)
{
    vec3 fragToLight = FragPos - lightPos;
    float currentDepth = length(fragToLight);

    float shadow = 0.0;
    float bias = 0.15; // 稍微大一点的 bias
    int samples = 20;  // 采样次数

    float viewDistance = length(viewPos - FragPos);

    // ✅ 动态半径：距离越远，阴影越软
    // / 25.0 是一个经验参数，你可以调整它来控制模糊程度
    float diskRadius = (1.0 + (viewDistance / far_plane)) / 25.0;

    for(int i = 0; i < samples; ++i)
    {
        // 采样：原来的方向 + 泊松偏移 * 半径
        float closestDepth = texture(shadowMap, fragToLight + sampleOffsetDirections[i] * diskRadius).r;

        // 还原单位
        closestDepth *= far_plane;   

        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }

    shadow /= float(samples);

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

    vec2 p = viewDir.xy * height_scale; // 每层的纹理坐标偏移
    vec2 deltaTexCoords = p / numLayers; // 每层的纹理坐标增量

    vec2 currentTexCoords = texCoords; // 当前层的纹理坐标
    float currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 当前层的深度贴图值

    while(currentLayerDepth < currentDepthMapValue)
    {
        currentTexCoords -= deltaTexCoords; // 移动到下一层
        currentLayerDepth += LayerDepth; // 增加当前层的深度
        currentDepthMapValue = texture(depthMap, currentTexCoords).r; // 获取新层的深度贴图值
    }
    // return currentTexCoords; // 返回最终的纹理坐标

    vec2 prevTexCoords = currentTexCoords + deltaTexCoords; // 上一层的纹理坐标

    float afterDepth = currentDepthMapValue - currentLayerDepth; // 当前层的深度差
    float beforeDepth = texture(depthMap, prevTexCoords).r - (currentLayerDepth - LayerDepth); // 上一层的深度差

    float weight = afterDepth / (afterDepth - beforeDepth); // 线性插值权重

    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight); // 线性插值计算最终纹理坐标

    // return finalTexCoords; // 返回最终的纹理坐标

    // ✅ 新增 - 视差自阴影 (找光线遮挡)
    vec2 P_Light = lightDir.xy * height_scale; // 光线方向的纹理坐标偏移
    vec2 deltaTexCoordsLight = P_Light / numLayers; // 每层的光线纹理坐标增量

    float currentLayerDepthLight = texture(depthMap, finalTexCoords).r; // 最终层的深度贴图值
    vec2 currentTexCoordsLight = finalTexCoords; // 最终层的纹理坐标

    float shadowAccumulation = 0.0; // 积累阴影
    int shadowSamples = 0; // 采样计数

    while(currentLayerDepthLight > 0.0)
    {
        currentTexCoordsLight += deltaTexCoordsLight; // 沿光线方向移动
        currentLayerDepthLight -= LayerDepth; // 增加当前层的深度

        float currentDepthMapValueLight = texture(depthMap, currentTexCoordsLight).r; // 获取新层的深度贴图值

        // 判定：如果【光线现在的深度】 > 【当前地形深度】
        // 说明光线还在地形下面（被挡住了！）
        if(currentLayerDepthLight > currentDepthMapValueLight)
        {
            // 被挡住了！累加阴影
            // 柔和阴影：累加权重
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
    // ✅ 从视差贴图中获取切线空间的高度
    vec3 viewDir_Tangent = normalize(fs_in.TangentViewPos - fs_in.TangentFragPos);
    vec3 lightDir_Tangent = normalize(fs_in.TangentLightPos - fs_in.TangentFragPos);
    float parallaxShadow; // 输出变量
    vec2 texCoords = parallaxMapping(fs_in.TexCoords, viewDir_Tangent, lightDir_Tangent, parallaxShadow);

    if(texCoords.x > 1.0 || texCoords.y > 1.0 || texCoords.x < 0.0 || texCoords.y < 0.0)
        discard; // 丢弃超出范围的片段

    // ✅ 从法线贴图中获取切线空间的法线
    vec3 normal = texture(normalMap, texCoords).rgb;
    normal = normal * 2.0 - 1.0; // 从 [0,1] 转换到 [-1,1]
    // FragColor = vec4(normal * 0.5 + 0.5, 1.0);
    normal = normalize(fs_in.TBN * normal); // 转换到世界空间

    // 采样颜色
    vec3 color = texture(diffuseTexture, texCoords).rgb;

    // vec3 lightColor = vec3(1.0);
    // Ambient
    vec3 ambient = 0.05 * lightColor;
    // Diffuse
    vec3 lightDir = normalize(lightPos - fs_in.FragPos);
    float diff = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = diff * lightColor;
    // Specular
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    float spec = 0.0;
    if(blinn)
    {
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    }
    else
    {
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0);
    }
    vec3 specular = spec * lightColor;
    // 实现衰弱
    float distance = length(lightPos - fs_in.FragPos);
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    float attenuation = 1.0/(constant + linear * distance + quadratic * distance * distance);
    ambient  *= attenuation; 
    diffuse  *= attenuation;
    specular *= attenuation;
    // Calculate shadow
    float shadow = ShadowCalculation(fs_in.FragPos);
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular) * (1.0 - parallaxShadow)) * color;

    FragColor = vec4(lighting, 1.0);

    float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = FragColor;
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}