#version 430 core

layout (local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Particle
{
    vec4 Position; // w: 0.0=下落, 1.0=飞溅
    vec4 Velocity; // w: 飞溅生命周期倒计时
};

layout (std430, binding = 0) buffer ParticleBuffer
{
    Particle particles[];
};

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

uniform float deltaTime;
// 🌟 新增：系统时间，用于让风力随时间波动
uniform float time;

uniform vec3 cameraPos;

uniform sampler2D gPositionMap;
// 🌟 [新增] 引入法线贴图，用于计算水花反弹方向
uniform sampler2D gNormalMap;

// 🌟 核心修复：工业级整数哈希算法 (Wang Hash)
// 专门解决 GPU 中由于大数字引起的浮点数精度坍塌和条纹聚集问题
uint wang_hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// 将无符号整数转换为 0.0 到 1.0 之间的浮点数
float randomFloat(uint seed)
{
    return float(seed) * (1.0 / 4294967296.0);
}

void main()
{
    uint index = gl_GlobalInvocationID.x;
    Particle p = particles[index];

    // 提取状态机变量 (榨干 w 通道)
    float state = p.Position.w; 
    float lifeTime = p.Velocity.w;

    // 获取随机种子
    uint seedState = index + floatBitsToUint(time);
    uint seedX = wang_hash(seedState * 1973u);
    uint seedY = wang_hash(seedState * 9277u);
    uint seedZ = wang_hash(seedState * 26699u);

    // ==========================================
    // 🌧️ 状态 0：下落阶段
    // ==========================================
    if (state < 0.5) 
    {
        float windStrength = 15.0 + sin(time * 2.0) * 5.0; 
        vec3 windDir = vec3(1.0, 0.0, -0.2); 
        vec3 currentVelocity = p.Velocity.xyz + windDir * windStrength;

        p.Position.xyz += currentVelocity * deltaTime;

        bool collided = false;
        vec3 hitNormal = vec3(0.0, 1.0, 0.0);

        // 1. 绝对地面碰撞
        if (p.Position.y < 0.0) {
            collided = true;
            p.Position.y = 0.01;
        }
        else 
        {
            // 2. G-Buffer 深度碰撞
            vec4 clipSpace = projection * view * vec4(p.Position.xyz, 1.0);
            if (clipSpace.w > 0.0) 
            {
                vec3 ndc = clipSpace.xyz / clipSpace.w;
                vec2 uv = ndc.xy * 0.5 + 0.5; 

                if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) 
                {
                    // 🌟 核心修复：先拿法线！只有法线长度 > 0.5，才说明这是真正的建筑！
                    vec3 sampledNormal = texture(gNormalMap, uv).xyz;
                    if (length(sampledNormal) > 0.5) 
                    {
                        vec3 scenePos = texture(gPositionMap, uv).xyz;
                        float sceneDist = length(scenePos - cameraPos);
                        float particleDist = length(p.Position.xyz - cameraPos);

                        // 🌟🌟🌟 终极修复：增加深度“厚度(Thickness)”检测！
                        // 算出粒子穿过表面的深度差
                        float thickness = particleDist - sceneDist;
                        
                        // 只有当粒子刚好穿透表面 (0.0 到 1.5 米内)，才判定为真实碰撞！
                        // 如果大于 1.5 米，说明它只是恰好在背景里被前景的楼挡住了，绝对不能吸附过来！
                        if (thickness > 0.0 && thickness < 1.5) {
                            collided = true;
                            hitNormal = normalize(sampledNormal); 
                            p.Position.xyz = scenePos + hitNormal * 0.05;
                        }
                    }
                }
            }
        }

        // 🌟 状态转换：从 下落 -> 飞溅
        if (collided)
        {
            p.Position.w = 1.0; 
            p.Velocity.w = 0.15 + randomFloat(seedX) * 0.15; 
            
            vec3 bounceDir = normalize(hitNormal + vec3(randomFloat(seedX)-0.5, randomFloat(seedY)*0.5, randomFloat(seedZ)-0.5));
            float bounceForce = 2.0 + randomFloat(seedY) * 3.0; 
            p.Velocity.xyz = bounceDir * bounceForce;
        }
    }
    // ==========================================
    // 💦 状态 1：飞溅阶段 (微型抛物线)
    // ==========================================
    else 
    {
        p.Velocity.y -= 15.0 * deltaTime; 
        p.Position.xyz += p.Velocity.xyz * deltaTime;
        
        p.Velocity.w -= deltaTime;

        if (p.Velocity.w <= 0.0) 
        {
            p.Position.w = 0.0; 
            p.Position.x = cameraPos.x + (randomFloat(seedX) - 0.5) * 150.0;
            p.Position.y = cameraPos.y + 20.0 + randomFloat(seedY) * 30.0;
            p.Position.z = cameraPos.z + (randomFloat(seedZ) - 0.5) * 150.0;
            p.Velocity.xyz = vec3(0.0, -25.0, 0.0); 
        }
    }

    particles[index] = p;
}