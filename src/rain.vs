#version 430 core

struct Particle
{
    vec4 Position;
    vec4 Velocity;
};

layout (std430, binding = 0) buffer ParticleBuffer
{
    Particle particles[];
};

// 🌟🌟🌟 新增：把 21 盏灯的 SSBO 也接进来！
struct Light
{
    vec4 Position; // w: Radius
    vec4 Color;    // w: Intensity
};

layout (std430, binding = 1) buffer LightBuffer
{
    Light lights[];
};

uniform int activePointLightsCount; // C++ 传进来的灯光总数

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

// 同样需要传入时间，保证算风力拉伸时一致
uniform float time;

// 🌟 新增：摄像机位置
uniform vec3 cameraPos;

// 传递给片元着色器的变量
out vec3 ParticleColor;
out float ParticleAlpha;

void main()
{
    // 🌟 经典回归：一个粒子拆成 2 个顶点，一头一尾画成一条线！
    uint particleIndex = gl_VertexID / 2;
    uint isTail = gl_VertexID % 2; 

    Particle p = particles[particleIndex];
    vec3 pos = p.Position.xyz;
    float state = p.Position.w; 
    float lifeTime = p.Velocity.w; 
    
    // 距离淡出，防止远处的雨闪烁
    float dist = distance(cameraPos, pos);
    float fade = 1.0 - smoothstep(10.0, 80.0, dist);

    // ==========================================
    // 🎨 动态光照：霓虹染色 (保持我们的神级打光)
    // ==========================================
    // 🌟 调优 1：降低环境基础冷色，让没有灯光的地方雨水更加低调隐蔽
    vec3 rainIllumination = vec3(0.02, 0.04, 0.08); 

    for(int j = 0; j < activePointLightsCount; ++j) 
    {
        vec3 lightPos = lights[j].Position.xyz;
        float radius = lights[j].Position.w;
        vec3 lightCol = lights[j].Color.xyz;
        float intensity = lights[j].Color.w;

        float distToLight = distance(pos, lightPos);

        if (distToLight < radius) 
        {
            float attenuation = 1.0 / (distToLight * distToLight + 1.0);
            float factor = (distToLight * distToLight) / (radius * radius);
            float falloff = clamp(1.0 - factor * factor, 0.0, 1.0);
            
            // 🌟 调优 2：稍微压暗霓虹灯的染色乘数，防止过亮变成发光棒
            rainIllumination += lightCol * (intensity * 0.08) * attenuation * falloff;
        }
    }
    ParticleColor = rainIllumination;

    // ==========================================
    // 📐 物理形态拉伸 (Motion Blur 几何化)
    // ==========================================
    if (state < 0.5) 
    {
        // 🌧️ 表现 0：下落的雨丝
        float windStrength = 15.0 + sin(time * 2.0) * 5.0;
        vec3 windDir = vec3(1.0, 0.0, -0.2);
        vec3 currentVelocity = vec3(0.0, -25.0, 0.0) + windDir * windStrength;

        if (isTail == 1u) {
            // 🌟 调优 3：大幅缩短雨丝的物理长度！从 0.06 降到 0.025，使其变回秀气的细雨
            float streakLength = 0.025; 
            pos -= currentVelocity * streakLength;
        }

        // 🌟 调优 4：降低雨滴的基础透明度！从 0.6 降到 0.25，恢复半透明水质感
        ParticleAlpha = (isTail == 1u ? 0.0 : 0.25) * fade;
    }
    else 
    {
        // 💦 表现 1：砸在地上弹起的微小水针！(极其真实)
        if (isTail == 1u) {
            // 保持水针短短的炸开感
            pos -= p.Velocity.xyz * 0.015; 
        }

        // 🌟 调优 5：稍微压低水花弹射的透明度，避免抢占过多视线
        float splashAlpha = smoothstep(0.0, 0.15, lifeTime) * 0.8; 
        ParticleAlpha = (isTail == 1u ? 0.0 : splashAlpha) * fade;
    }

    gl_Position = projection * view * vec4(pos, 1.0);
}