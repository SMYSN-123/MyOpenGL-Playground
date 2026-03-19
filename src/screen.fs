#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform float offset_x;
uniform float offset_y;

uniform sampler2D screenTexture;     // TEXTURE0: HDR 原图
uniform sampler2D bloomBlur;         // TEXTURE1: 物理泛光图
uniform sampler2D dirtMaskTexture;   // TEXTURE2: 镜头污渍贴图 (黑底白灰尘)

uniform float exposure;

// --- 艺术控制参数 ---
// 泛光强度 (建议 0.03 ~ 0.15 之间)
uniform float bloomStrength = 0.05; 
// 污渍显示强度 (控制灰尘被照亮时的明显程度)
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

void main()
{
    vec2 offsets[9] = vec2[](
        vec2(-offset_x, offset_y),
        vec2(0.0f, offset_y),
        vec2(offset_x, offset_y),
        vec2(-offset_x, 0.0f),
        vec2(0.0f, 0.0f),
        vec2(offset_x, 0.0f),
        vec2(-offset_x, -offset_y),
        vec2(0.0f, -offset_y),
        vec2(offset_x, -offset_y)
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

    // float average = 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
    // col = vec3(average);

    // --- 🔥 叠加泛光 (Bloom) 🔥 ---
    // 在 Tone Mapping 之前叠加
    if(bloom)
    {
        vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
        vec3 dirtMask = texture(dirtMaskTexture, TexCoords).rgb * dirtMaskIntensity;
        vec3 bloomWithDirt = bloomColor + (bloomColor * dirtMask);
        col += bloomWithDirt * bloomStrength;
    }

    col *= exposure; // 曝光调整
    col = ACESFilm(col); // ACES 色调映射

    col = pow(col, vec3(1.0 / 2.2)); // Gamma correction

    FragColor = vec4(col, 1.0);
}