#version 430 core
out vec4 FragColor;

in vec2 TexCoords;

// --- 纹理采样器 ---
uniform sampler2D gPosition;      // G-Buffer 中的世界坐标
uniform sampler2DArray shadowMap; // CSM 级联阴影贴图

// --- 🌟 [新增] 挂载包含 21 盏灯的 SSBO 硬盘！ ---
struct Light
{
    vec4 Position; // xyz: 坐标, w: 衰减半径 (Radius)
    vec4 Color;    // rgb: 颜色, w: 发光强度 (Intensity)
};
layout (std430, binding = 1) buffer LightBuffer
{
    Light lights[];
};
uniform int activePointLightsCount; // C++ 传进来的灯光总数

// --- 矩阵 UBO ---
layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

layout (std140) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[16];
};

uniform float cascadePlaneDistances[16];
uniform int cascadeCount;

// --- 光照与相机参数 ---
uniform vec3 viewPos;
uniform vec3 lightDir;
uniform vec3 lightColor;

// ==========================================
// 1. 基础工具函数
// ==========================================

// 交错梯度噪声 (IGN) - 用于打散步进产生的“断层切片”
float IGN(vec2 pixelPos)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pixelPos, magic.xy)));
}

// HG 相函数 - 决定光柱迎着光亮，背着光暗
float PhaseFunctionHG(float cosTheta, float g)
{
    float g2 = g * g;
    float num = 1.0 - g2;
    float denom = 4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

// ==========================================
// 2. 查 CSM 阴影
// ==========================================
float ShadowCalculation(vec3 fragPosWorld)
{
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
    }
    if(layer == -1) layer = cascadeCount - 1;

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorld, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;
    if (currentDepth > 1.0) return 0.0;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0) return 0.0;

    float bias = 0.005;
    if (layer > 0) bias *= (layer + 1.0) * 0.5; 

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, float(layer))).r; 
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
    return shadow;
}

// ==========================================
// 3. 核心：光线步进 (Raymarching)
// ==========================================
void main()
{
    // 1. 获取基础几何信息
    vec3 worldPos = texture(gPosition, TexCoords).rgb;
    float depth = length(worldPos - viewPos);
    
    // 如果是天空，给一个合理的极大值，但不要产生断层
    bool isSky = (length(worldPos) < 0.1);
    float maxRayDist = isSky ? 100.0 : depth;
    
    // 限制体积光的有效范围，防止远处天空过曝
    float totalDist = min(maxRayDist, 60.0); 

    // 2. 步进准备
    vec3 rayDir = normalize(isSky ? (inverse(view) * vec4(normalize(vec3(TexCoords * 2.0 - 1.0, -1.0)), 0.0)).xyz : (worldPos - viewPos));
    
    int STEP_COUNT = 30;
    float stepSize = totalDist / float(STEP_COUNT);

    // Dither 抖动防止色带
    float dither = IGN(gl_FragCoord.xy);
    vec3 currentPos = viewPos + rayDir * stepSize * dither;

    // 3. 物理参数定义
    float scatteringCoeff = 0.1; // 散射强度
    float extinctionCoeff = 0.05; // 消光/吸收强度

    // 🌟 修改：把相函数重命名为 phaseDir，专属于月光
    float cosThetaDir = dot(rayDir, normalize(lightDir));
    float phaseDir = PhaseFunctionHG(cosThetaDir, 0.7);

    vec3 accumulation = vec3(0.0);
    float transmittance = 1.0; // 初始透射率为 100%

//     // 4. 核心物理步进
//     for(int i = 0; i < STEP_COUNT; i++)
//     {
//         // A. 先准备一个“空篮子”，用来装这个空气点收集到的所有散射光
//         vec3 stepScattering = vec3(0.0);

//         float shadow = ShadowCalculation(currentPos);
        
//         if(shadow < 0.5)
//         {
//             // [关键点] 只有没被遮挡的地方才产生散射
//             vec3 scatteringLight = lightColor * phase * scatteringCoeff;
            
//             // [物理公式] 当前步进点对相机的贡献 = 散射光 * 当前透射率
//             // 考虑这一段路程内的吸收
//             float stepTransmittance = exp(-extinctionCoeff * stepSize);
            
//             // 能量积分：让亮度平滑增长
//             vec3 integral = (scatteringLight - scatteringLight * stepTransmittance) / extinctionCoeff;
//             accumulation += transmittance * integral;
            
//             // 更新总透射率
//             transmittance *= stepTransmittance;
//         }
//         else
//         {
//             // 即使在阴影里，光也在被吸收（只是没有新的光散射进来）
//             transmittance *= exp(-extinctionCoeff * stepSize);
//         }

//         currentPos += rayDir * stepSize;
        
//         // 性能优化：如果光已经衰减到看不见了，提前跳出
//         if (transmittance < 0.01) break;
//     }

//     // 5. 最终合成
//     // 这里不再需要 * 0.02 这种玄学数字，物理公式本身会约束强度
//     FragColor = vec4(accumulation, 1.0); 
// }

// 4. 🌟🌟🌟 架构革新：统一物理积分 🌟🌟🌟
    for(int i = 0; i < STEP_COUNT; i++)
    {
        // A. 先准备一个“空篮子”，用来装这个空气点收集到的所有散射光
        vec3 stepScattering = vec3(0.0);

        // B. 【轨道 1】：月光阴影采集
        float shadow = ShadowCalculation(currentPos);
        if(shadow < 0.5) {
            // 月光没被挡住，往篮子里丢光！
            stepScattering += lightColor * phaseDir * scatteringCoeff;
        }

        // C. 【轨道 2】：点光源/霓虹灯采集
        for(int j = 0; j < activePointLightsCount; ++j)
        {
            vec3 ptPos = lights[j].Position.xyz;
            float ptRadius = lights[j].Position.w; // 拿半径
            vec3 ptCol = lights[j].Color.xyz;
            float ptIntensity = lights[j].Color.w; // 拿强度

            vec3 toLight = ptPos - currentPos;
            float distSq = dot(toLight, toLight);
            
            // 【早退优化】如果不在这盏灯的照亮半径内，直接无视！极省性能！
            if (distSq < ptRadius * ptRadius) 
            {
                // 1. UE4 平滑截断距离衰减
                float factor = distSq / (ptRadius * ptRadius);
                float falloff = clamp(1.0 - factor * factor, 0.0, 1.0);
                float attenuation = (1.0 / (distSq + 1.0)) * (falloff * falloff) * ptIntensity;

                // 2. 🌟 手撕光锥数学公式 (路灯专属)
                float coneAttenuation = 1.0;
                if (j < 11) { // 规定前 11 个是带灯罩的路灯
                    vec3 spotDir = vec3(0.0, -1.0, 0.0); // 笔直朝下
                    vec3 currentToLight = normalize(currentPos - ptPos); // 从灯指向空气点
                    float cosAngle = dot(currentToLight, spotDir);
                    // 0.75 到 0.85 产生柔和的光柱边缘
                    coneAttenuation = smoothstep(0.75, 0.85, cosAngle); 
                }
                attenuation *= coneAttenuation;

                // 3. 计算点光源专属的相函数 (迎着霓虹看更亮)
                if (attenuation > 0.001) {
                    float cosThetaPt = dot(rayDir, normalize(toLight));
                    float phasePt = PhaseFunctionHG(cosThetaPt, 0.5); // g=0.5 柔和微芒

                    // 这盏霓虹灯照亮了这里！往篮子里丢光！
                    stepScattering += ptCol * phasePt * scatteringCoeff * attenuation;
                }
            }
        }

        // D. 🌟 统一物理消光与积分！(不管光从哪来，统一被空气吸收)
        float stepTransmittance = exp(-extinctionCoeff * stepSize);
        
        // 你的原始微积分公式，现在完美适配多光源！
        vec3 integral = (stepScattering - stepScattering * stepTransmittance) / extinctionCoeff;
        accumulation += transmittance * integral;
        
        transmittance *= stepTransmittance;

        currentPos += rayDir * stepSize;
        
        // 极暗打断
        if (transmittance < 0.01) break;
    }

    // 这里稍微乘个系数压暗一点，防止那么多灯加起来过曝。你可以自己在 C++ 慢慢调
    FragColor = vec4(accumulation * 0.05, 1.0); 
}