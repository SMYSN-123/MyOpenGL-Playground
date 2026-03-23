#version 330 core
layout (location = 0) out vec4 FragColor;

in vec2 TexCoords;

// --- 🌟 核心：G-Buffer 纹理采样器 ---
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo_parallaxShadow;
uniform sampler2D gORM;

// --- 相机与矩阵 ---
layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view; // CSM 阴影计算依然需要 View 矩阵来判断深度级联
};

layout (std140) uniform LightSpaceMatrices
{
    mat4 lightSpaceMatrices[16];
};

// --- IBL 环境光贴图 ---
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D   brdfLUT;

// --- CSM 阴影专用 ---
uniform sampler2DArray shadowMap;
uniform float cascadePlaneDistances[16];
uniform int cascadeCount;

// --- 光照参数 (定向光) ---
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 lightColor;

const bool DEBUG_CSM_LAYER = false;
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
    float r = roughness + 1.0;
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

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ShadowCalculation(vec3 fragPosWorld, vec3 normal, out vec3 debugColor)
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

    if(layer == 0) debugColor = vec3(1, 0, 0);
    else if(layer == 1) debugColor = vec3(0, 1, 0);
    else if(layer == 2) debugColor = vec3(0, 0, 1);
    else debugColor = vec3(1, 1, 0);

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorld, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;
    if (currentDepth > 1.0) return 0.0;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x > 1.0 || projCoords.x < 0.0 || projCoords.y > 1.0 || projCoords.y < 0.0) return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
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
// 主函数：优雅的数据解包与计算
// ==========================================
void main()
{
    // 🌟 1. 从 G-Buffer 中“解包”数据 (就像读取照片像素一样简单)
    vec3 FragPos = texture(gPosition, TexCoords).rgb;
    vec3 N       = texture(gNormal, TexCoords).rgb;
    
    // 💡 优雅的小技巧：跳过背景(天空盒)的光照计算
    // 如果法线长度接近0，说明这里没有模型渲染过，直接丢弃或输出默认颜色
    if (length(N) < 0.1) discard; 

    vec4 albedo_shadow = texture(gAlbedo_parallaxShadow, TexCoords);
    vec3 albedo        = albedo_shadow.rgb;
    float parallaxShadow = albedo_shadow.a;

    vec3 orm       = texture(gORM, TexCoords).rgb;
    float ao       = orm.r;
    float roughness= max(orm.g, 0.04); // 避免纯粹的高光除零错误
    float metallic = clamp(orm.b, 0.0, 1.0);

    // 🌟 2. 准备 PBR 需要的向量 (全部都在世界空间下！)
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 🌟 3. 直接光照 (Dir Light)
    float attenuation = 1.0;
    vec3 radiance = lightColor * attenuation;

    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = D_GGX_TR(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = nominator / denominator;

    vec3 KS = F;
    vec3 KD = vec3(1.0) - KS;
    KD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (KD * albedo / PI + specular) * radiance * NdotL;

    // 🌟 4. 间接光照 (IBL)
    vec3 KS_ambient = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 KD_ambient = 1.0 - KS_ambient;
    KD_ambient *= 1.0 - metallic;

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse_ambient = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular_ambient = prefilteredColor * (KS_ambient * envBRDF.x + envBRDF.y);

    vec3 ambient = (KD_ambient * diffuse_ambient + specular_ambient) * ao;

    // 🌟 5. 阴影计算与最终合成
    vec3 debugCascadeColor = vec3(0.0);
    float shadow = ShadowCalculation(FragPos, N, debugCascadeColor);
    
    // 综合 CSM 阴影和视差自阴影
    float visibility = (1.0 - shadow) * (1.0 - parallaxShadow);

    vec3 color = ambient + Lo * visibility;

    // 🌟 6. 输出最终颜色 (也可以在这里加 HDR 色调映射 Tone Mapping)
    if(DEBUG_CSM_LAYER)
    {
        FragColor = vec4(color * 0.5 + debugCascadeColor * 0.5, 1.0);
    }
    else
    {
        FragColor = vec4(color, 1.0);
    }
}