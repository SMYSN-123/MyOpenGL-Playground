#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D ssrTraceTexture;  // RG: Reflected UV, B: Visibility
uniform sampler2D hdrColorCopyTexture;  // 备份的 HDR 原图 (自带 Mipmap)
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gORM;
uniform sampler2D gAlbedo_parallaxShadow; // 额外传入 Albedo 和 Parallax Shadow 数据

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

// PBR 菲涅尔方程
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // 1. 读取基础数据
    vec3 originalColor = texture(hdrColorCopyTexture, TexCoords).rgb;

    // 🌟 【修改】我们不再只采样一次 SSR 数据，而是做一个 3x3 的盒型模糊 (Box Blur)
    vec2 texelSize = 1.0 / textureSize(ssrTraceTexture, 0); // 获取一个像素的大小
    vec4 ssrData = vec4(0.0);
    
    // 收集周围 9 个像素的数据并求和
    for(int x = -1; x <= 1; ++x) 
    {
        for(int y = -1; y <= 1; ++y) 
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            ssrData += texture(ssrTraceTexture, TexCoords + offset);
        }
    }
    // 取平均值！噪点被抹平了
    ssrData /= 9.0;

    vec2 refUV = ssrData.xy;
    float visibility = ssrData.b;

    // 如果完全没命中，直接退回原图，省性能
    if (visibility <= 0.01) { 
        FragColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 worldNormal = texture(gNormal, TexCoords).rgb;
    vec3 worldPos = texture(gPosition, TexCoords).rgb;
    vec4 orm = texture(gORM, TexCoords);
    float roughness = orm.g;
    float metallic = orm.b;
    float puddleMask = orm.a;

    mat4 invView = inverse(view);
    vec3 cameraPos = vec3(invView[3]); 
    vec3 V = normalize(cameraPos - worldPos);
    vec3 N = normalize(worldNormal);

    // 2. 获取 SSR 的物理模糊反射色
    float MAX_REFLECTION_LOD = 6.0;
    float lodLevel = roughness * MAX_REFLECTION_LOD;
    vec3 reflectedColor = textureLod(hdrColorCopyTexture, refUV, lodLevel).rgb;

    // 3. 计算微表面菲涅尔 (Fresnel)
    vec3 albedo = texture(gAlbedo_parallaxShadow, TexCoords).rgb;
    vec3 F0 = mix(vec3(0.04), albedo, metallic); 
    F0 = mix(F0, vec3(0.02), puddleMask); // 积水区域的 F0
    
    // Schlick 近似计算当前视角的反射强度
    vec3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);

    // 4. 🌟 修复物理混合逻辑 🌟
    // SSR 算出来的高光能量
    vec3 ssrSpecular = reflectedColor * F;

    // 因为 originalColor 中【已经】包含了天空盒的 IBL 反射
    // 当 SSR 命中时，它应该【覆盖】掉原来的 IBL 反射，而不是把整张图压暗。
    // 在屏幕空间最平滑的做法是：基于 SSR 命中权重，用 SSR 的颜色替换原本画面中“应该是倒影”的那部分能量。
    
    float validSSRWeight = visibility * smoothstep(0.6, 0.2, roughness);

    // 直接在原图基础上，叠加 SSR 能量，并用 F 来限制它不要过曝
    // 这样既保留了漫反射底色，又完美融合了反射
    vec3 finalColor = originalColor;
    if (validSSRWeight > 0.0) 
    {
        // 软覆盖混合：按权重把原图平滑过渡到"叠加了 SSR 倒影"的画面
        vec3 mixTarget = originalColor + ssrSpecular; 
        finalColor = mix(originalColor, mixTarget, validSSRWeight);
    }

    // 暴力输出！
    FragColor = vec4(finalColor, 1.0);
}