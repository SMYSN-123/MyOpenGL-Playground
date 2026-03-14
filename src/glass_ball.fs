#version 330 core
out vec4 FragColor;

in vec3 WorldPos;
in vec3 Normal;
in vec2 TexCoords;

// 摄像机位置
uniform vec3 camPos;

// --- 材质参数 ---
// 0 = 纯参数 PBR (用于镜子等)
// 1 = 高级折射玻璃 (带色散)
uniform int u_MaterialType; 

uniform vec3  u_AlbedoVal;
uniform float u_MetallicVal;
uniform float u_RoughnessVal;

// --- IBL 环境贴图 ---
uniform samplerCube irradianceMap; // 漫反射环境光
uniform samplerCube prefilterMap;  // 镜面反射环境光 (用于反射和折射采样)
uniform sampler2D   brdfLUT;       // BRDF 查找表

const float PI = 3.14159265359;

// 菲涅尔方程 (Schlick近似)
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 考虑粗糙度的菲涅尔
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec3 N = normalize(Normal);
    vec3 V = normalize(camPos - WorldPos);
    vec3 R = reflect(-V, N); // 反射向量

    // 基础反射率，绝缘体通常是 0.04，金属则是其本身的颜色
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, u_AlbedoVal, u_MetallicVal);

    vec3 finalColor = vec3(0.0);

    if (u_MaterialType == 0) {
        // ==========================================
        // 模式 0: 标准 PBR (完美镜子走这里)
        // ==========================================

        float cosTheta = max(dot(N, V), 0.0);
        vec3 F = fresnelSchlickRoughness(cosTheta, F0, u_RoughnessVal);

        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - u_MetallicVal;	  

        // IBL 漫反射 (Irradiance)
        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuse    = irradiance * u_AlbedoVal;

        // IBL 镜面反射 (Prefilter)
        const float MAX_REFLECTION_LOD = 4.0;
        vec3 prefilteredColor = textureLod(prefilterMap, R, u_RoughnessVal * MAX_REFLECTION_LOD).rgb;    
        vec2 brdf  = texture(brdfLUT, vec2(max(dot(N, V), 0.0), u_RoughnessVal)).rg;
        vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

        // 镜子的 Metallic = 1，所以 kD = 0，只有 specular 起作用，完美反射环境！
        vec3 ambient = (kD * diffuse + specular); 
        finalColor = ambient; 

    }
    else if (u_MaterialType == 1)
    {
        // ==========================================
        // 模式 1: 高级折射玻璃 (带色散)
        // ==========================================

        // 1. 计算菲涅尔效应 (决定多少光被反射，多少光被折射)
        // 视线正对玻璃时，透射多；视线与玻璃平行时，反射多。
        float cosTheta = min(max(dot(V, N), 0.0), 1.0);
        vec3 F = fresnelSchlick(cosTheta, F0); 

        // 2. 采样环境反射 (天空盒)
        // 玻璃非常光滑，所以我们采样 Lod 0
        vec3 reflectionColor = textureLod(prefilterMap, R, 0.0).rgb;

        // 3. 计算色散折射 (Chromatic Aberration)
        // 玻璃的折射率 (IOR) 大约是 1.52。空气是 1.0。
        // 比值 eta = 1.0 / 1.52 = 0.657
        // 我们为红绿蓝分别设置稍微不同的折射率，制造物理色散
        // float ior_R = 0.650; // 红光折射率比值
        // float ior_G = 0.657; // 绿光折射率比值
        // float ior_B = 0.665; // 蓝光折射率比值
        
        float ior_R = 0.98; // 红光折射率比值
        float ior_G = 0.99; // 绿光折射率比值
        float ior_B = 1.00; // 蓝光折射率比值

        vec3 refractVec_R = refract(-V, N, ior_R);
        vec3 refractVec_G = refract(-V, N, ior_G);
        vec3 refractVec_B = refract(-V, N, ior_B);

        // 分别采样三种颜色的环境贴图
        float r = textureLod(prefilterMap, refractVec_R, 0.0).r;
        float g = textureLod(prefilterMap, refractVec_G, 0.0).g;
        float b = textureLod(prefilterMap, refractVec_B, 0.0).b;
        vec3 refractionColor = vec3(r, g, b);

        // 给玻璃加一点点基色（比如微微发蓝/绿的厚玻璃感），如果是纯白则为完全透明
        refractionColor *= u_AlbedoVal;

        // 4. 混合反射与折射
        // F 代表反射的比例，(1.0 - F) 代表折射的比例
        finalColor = mix(refractionColor, reflectionColor, F);
    }

    // HDR 色调映射 (Tone Mapping) 和 Gamma 校正
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0/2.2)); 

    FragColor = vec4(finalColor, 1.0); // 玻璃在这里不需要 alpha 半透明，因为背景已经被折射进来了！
}