#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform float offset_x;
uniform float offset_y;

uniform sampler2D screenTexture;     // TEXTURE0: HDR 原图
uniform sampler2D bloomBlur;         // TEXTURE1: 物理泛光图
uniform sampler2D dirtMaskTexture;   // TEXTURE2: 镜头污渍贴图 (黑底白灰尘)

uniform int numLights; // 场景中实际点光源数量
uniform vec2 lightsPos[32];   // 点光源位置列表
uniform vec3 lightsColor[32]; // 点光源颜色列表

uniform float exposure;

// --- 艺术控制参数 ---
uniform float bloomStrength = 0.04; 
uniform float dirtMaskIntensity = 5.0; 
uniform bool bloom;

// ACES 色调映射算法
vec3 ACESFilm(vec3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// 提取极高亮像素
vec3 GetFlareSource(sampler2D tex, vec2 uv) {
    vec3 color = texture(tex, uv).rgb; 
    float brightness = max(color.r, max(color.g, color.b));
    float contribution = max(0.0, brightness - 2.0);
    return color * (contribution / max(brightness, 0.00001));
}

// 🌟 竖向平顶正六边形 SDF (带物理色差)
vec3 HexagonalGhost(vec2 uv, vec2 center, float size, float thickness) {
    vec2 aspectCor = vec2(offset_y / offset_x, 1.0); 
    
    // 物理色散：红蓝通道错开
    vec2 offsetR = vec2(0.003, 0.0) * aspectCor;
    vec2 offsetB = vec2(-0.003, 0.0) * aspectCor;

    vec2 pR = (uv + offsetR - center) * aspectCor;
    vec2 pG = (uv - center) * aspectCor;
    vec2 pB = (uv + offsetB - center) * aspectCor;

    // 完美的平顶正六边形公式
    float dR = max(abs(pR.y), abs(pR.y) * 0.5 + abs(pR.x) * 0.8660254);
    float dG = max(abs(pG.y), abs(pG.y) * 0.5 + abs(pG.x) * 0.8660254);
    float dB = max(abs(pB.y), abs(pB.y) * 0.5 + abs(pB.x) * 0.8660254);

    float r = smoothstep(size, size - 0.005, dR) * smoothstep(size - thickness, size - thickness + 0.005, dR);
    float g = smoothstep(size, size - 0.005, dG) * smoothstep(size - thickness, size - thickness + 0.005, dG);
    float b = smoothstep(size, size - 0.005, dB) * smoothstep(size - thickness, size - thickness + 0.005, dB);

    return vec3(r, g, b);
}

void main()
{
    vec2 offsets[9] = vec2[](
        vec2(-offset_x,  offset_y),
        vec2( 0.0f,      offset_y),
        vec2( offset_x,  offset_y),
        vec2(-offset_x,  0.0f),
        vec2( 0.0f,      0.0f),
        vec2( offset_x,  0.0f),
        vec2(-offset_x, -offset_y),
        vec2( 0.0f,     -offset_y),
        vec2( offset_x, -offset_y)
    );

    float kernel[9] = float[](
        0, -1, 0,
        -1, 5, -1,
        0, -1, 0
    );

    vec3 col = vec3(0.0);
    for(int i = 0; i < 9; i++)
    {
        col += vec3(texture(screenTexture, TexCoords.st + offsets[i])) * kernel[i];
    }

    // 2. 🎬 次世代多彩镜头光晕 (Spectral Flare)
    vec3 flare = vec3(0.0);
    if(bloom)
    {
        vec2 center = vec2(0.5, 0.5);
        vec3 flakes = vec3(0.0);

        // ==========================================
        // 🎬 坐标驱动：完美聚焦的六边形阵列
        // ==========================================
        for(int j = 0; j < numLights; j++) 
        {
            vec2 lightPos = lightsPos[j];

            // 🌟 1. 视线聚焦判定 (Foveated Check)：
            // 只有当光源靠近屏幕中心（你看着它）时，才产生六边形！
            float distToCenter = distance(lightPos, center);
            // 距离中心 0.1 以内最亮，超过 0.5 彻底消失
            float focus = smoothstep(0.5, 0.1, distToCenter); 
            if (focus < 0.01) continue; // 不看它就不渲染，画面干干净净！

            // 🌟 2. 遮挡判定：被楼挡住也不渲染
            vec3 sourceColor = texture(screenTexture, lightPos).rgb;
            float brightness = max(sourceColor.r, max(sourceColor.g, sourceColor.b));
            if (brightness < 2.0) continue; 
            
            float intensity = smoothstep(2.0, 5.0, brightness) * focus;

            vec2 dir = center - lightPos; 

            // 🌟 3. 增加数量！足足 14 个六边形镜片，贯穿整条对角线！
            const int NUM_FLAKES = 14;
            float scalars[NUM_FLAKES] = float[]( 
                0.15, 0.3, 0.45, 0.55, 0.7, 0.9, 1.1, 1.35, 1.6, 1.9,  // 光源对面
                -0.2, -0.4, -0.6, -0.9                                 // 光源同侧
            );
            
            for(int i = 0; i < NUM_FLAKES; i++) 
            {
                vec2 ghostPos = lightPos + dir * scalars[i];
                float size = 0.01 + abs(scalars[i]) * 0.025; // 离中心越远越大
                
                // 奇偶变换：实心六边形 vs 极具科技感的空心六边形线框！
                float thickness = (i % 3 == 0) ? 0.002 : size; 
                
                vec3 prismColor = HexagonalGhost(TexCoords, ghostPos, size, thickness);
                
                // 蓝橙冷暖交替，电影感标配
                vec3 tint = (i % 2 == 0) ? vec3(0.2, 0.6, 1.0) : vec3(1.0, 0.5, 0.2);
                
                flakes += prismColor * tint * intensity * (0.8 / (1.0 + abs(scalars[i])));
            }
        }
        
        // 叠加六边形序列
        flare += flakes * 1.5;

        col += flare * 0.4; 
    }

    // --- 叠加泛光 (Bloom) ---
    if(bloom)
    {
        vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
        vec3 dirtMask = texture(dirtMaskTexture, TexCoords).rgb * dirtMaskIntensity;

        // 🌟 终极细节：不仅泛光照亮污渍，绚丽的拉丝 (flare) 也要照亮镜头污渍！
        // 这会让拉丝经过灰尘时，产生非常漂亮的“星芒碎屑”感！
        vec3 bloomWithDirt = bloomColor + ((bloomColor + flare * 1.5) * dirtMask);

        col += bloomWithDirt * bloomStrength;
    }

    col *= exposure; // 曝光调整
    col = ACESFilm(col); // ACES 色调映射

    col = pow(col, vec3(1.0 / 2.2)); // Gamma 校正

    // 这个输出会被画到一张中间贴图上，而不是直接上屏幕
    FragColor = vec4(col, 1.0);
}