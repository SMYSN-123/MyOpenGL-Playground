#version 330 core
out vec4 FragColor; // RG: Reflected UV, B: Visibility, A: Visibility

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gORM;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

// SSR 参数控制 (可作为 Uniform 暴露给 C++)
const float maxDistance = 50.0;    // 最大反射距离
const float resolution  = 0.6;     // 步进精度 (0.1 ~ 1.0)
const int   steps       = 30;      // 精查二分步数
const float thickness   = 1.0;     // 物体厚度容忍度 (极其关键)

// 在 main 函数外面加上这个随机数生成器（极其经典的高频噪声函数）
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);
}

void main()
{
    // 1. 读取基础信息 (如果太粗糙，根本不反光，直接丢弃以优化性能)
    float roughness = texture(gORM, TexCoords).g;
    if (roughness > 0.5) // 这里的阈值可以根据实际情况调整，或者直接暴露为 Uniform
        discard;

    vec3 worldPos = texture(gPosition, TexCoords).rgb;
    vec3 worldNormal = texture(gNormal, TexCoords).rgb;
    if (length(worldNormal) < 0.1) // 忽略天空盒
        discard;

    // 2. 转换到观察空间 (View Space) - SSR 运算在 View Space 最稳定
    vec3 viewPos = (view * vec4(worldPos, 1.0)).xyz; // 转换到视空间
    vec3 viewNormal = mat3(view) * worldNormal; // 转换法线到视空间
    viewNormal = normalize(viewNormal);

    // 3. 计算反射光线 (View Space)
    vec3 viewDir = normalize(viewPos);
    vec3 rayDir = normalize(reflect(viewDir, viewNormal));

    // 4. 准备 3D 起点与终点
    vec3 startView = viewPos;
    vec3 endView = viewPos + rayDir * maxDistance;

    // 如果光线的终点跑到了摄像机的背后 (Z > 0)，会导致投影矩阵异常
    // 我们强行把它截断在摄像机前方一点点 (近裁剪面)
    if (endView.z > -0.1) 
    {
        float t = (-0.1 - startView.z) / rayDir.z;
        endView = startView + rayDir * t;
    }

    // 5. 投影到 2D 屏幕空间 (Clip Space -> NDC -> UV -> Fragment Coordinates)
    vec4 startClip = projection * vec4(startView, 1.0);
    vec4 endClip = projection * vec4(endView, 1.0);

    // 透视除法
    vec2 startNDC = startClip.xy / startClip.w;
    vec2 endNDC = endClip.xy / endClip.w;

    // 转换为 0~1 的 UV 坐标
    vec2 startUV = startNDC * 0.5 + 0.5;
    vec2 endUV   = endNDC * 0.5 + 0.5;

    // 转换为像素网格坐标 (为了 DDA 画线算法)
    vec2 texSize = textureSize(gPosition, 0);
    vec2 startFrag = startUV * texSize;
    vec2 endFrag = endUV * texSize;

    // 6. DDA 画线参数准备
    float deltaX = endFrag.x - startFrag.x;
    float deltaY = endFrag.y - startFrag.y;

    float useX = abs(deltaX) >= abs(deltaY) ? 1.0 : 0.0; // 选择主轴
    float delta = mix(abs(deltaX), abs(deltaY), useX) * clamp(resolution, 0.0, 1.0);

    vec2 increment = vec2(deltaX, deltaY) / max(delta, 0.001);

    // 🌟 【新增】获取当前像素的随机抖动值 (0.0 到 1.0 之间)
    float jitter = hash(gl_FragCoord.xy);

    // 7. 开启粗查 (Rough Pass)
    float search0 = 0.0;
    float search1 = 0.0;
    int hit0 = 0;

    // 🌟 【修改】给起步加上随机的步伐偏移！波纹瞬间碎成冰渣！
    vec2 frag = startFrag + increment * jitter; 
    vec2 uv = vec2(0.0);

    float viewDistance = 0.0;
    float depth = 0.0;

    for (int i = 0; i < int(delta); i++)
    {
        frag += increment;
        uv = frag / texSize;

        // 如果超出屏幕，立刻停止
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        // 更新进度百分比
        search1 = mix((frag.y - startFrag.y) / deltaY, (frag.x - startFrag.x) / deltaX, useX);

        // 🌟 核心防误判：如果是天空盒（无法线），光线直接无视它穿过去！防止假碰撞
        vec3 sampleNormal = texture(gNormal, uv).rgb;
        if (length(sampleNormal) < 0.1) 
        {
            search0 = search1; 
            continue;
        }

        // 获取场景在这个点的深度
        vec3 sampleWorldPos = texture(gPosition, uv).rgb;
        float sceneDepth = -(view * vec4(sampleWorldPos, 1.0)).z; // 场景深度 (View Space)

        // 【透视校正】插值当前光线深度
        viewDistance = (startView.z * endView.z) / mix(endView.z, startView.z, search1);
        float rayDepth = -viewDistance; // 取正距离

        // 计算差值
        depth = rayDepth - sceneDepth;

        // 碰撞判断
        if (depth > 0.0 && depth < thickness)
        {
            hit0 = 1; // 粗查命中
            break;
        }
        else
        {
            search0 = search1; // 更新上一个有效位置的百分比
        }
    }

    // 8. 开启精查二分查找 (Binary Search Pass)
    search1 = search0 + (search1 - search0) / 2.0;
    int hit1 = 0;

    int stepsToUse = steps * hit0; // 🌟 声明一个新变量来决定循环次数
    for (int i = 0; i < stepsToUse; i++) // 🌟 循环条件改为新变量
    {
        frag = mix(startFrag, endFrag, search1);
        uv = frag / texSize;

        vec3 sampleWorldPos = texture(gPosition, uv).xyz;
        float sceneDepth = -(view * vec4(sampleWorldPos, 1.0)).z;

        viewDistance = (startView.z * endView.z) / mix(endView.z, startView.z, search1);
        float rayDepth = -viewDistance;

        depth = rayDepth - sceneDepth;

        if (depth > 0.0 && depth < thickness)
        {
            hit1 = 1; // 精查命中
            search1 = search0 + (search1 - search0) / 2.0; // 二分更新
        }
        else
        {
            float temp = search1;
            search1 = search1 + (search1 - search0) / 2.0; // 二分更新
            search0 = temp;
        }
    }

    // 9. 计算可视度 (Visibility)
    float visibility = float(hit1);

    // [羽化A] 屏幕边缘防跳变羽化 + 越界裁切
    // 超出屏幕范围直接没收
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        visibility = 0.0;
    } else {
        // 在屏幕边缘 10% 的区域进行平滑淡出，防止镜头转动时倒影闪烁
        visibility *= smoothstep(0.0, 0.1, uv.x) * smoothstep(1.0, 0.9, uv.x);
        visibility *= smoothstep(0.0, 0.1, uv.y) * smoothstep(1.0, 0.9, uv.y);
    }

    // [羽化B] 距离迷雾衰减 
    // 🌟 修复：获取真实的物理碰撞点坐标，计算真正的飞行距离！
    vec3 hitWorldPos = texture(gPosition, uv).xyz;
    vec3 hitViewPos = (view * vec4(hitWorldPos, 1.0)).xyz;
    float currentDist = length(hitViewPos - startView);
    visibility *= (1.0 - clamp(currentDist / maxDistance, 0.0, 1.0));

    // [羽化C] 天空盒/无几何体过滤
    vec3 finalSampleNormal = texture(gNormal, uv).rgb;
    if (length(finalSampleNormal) < 0.1) { 
        visibility = 0.0; 
    }

    // [羽化E] 误差渐隐 (基于步进深度差)
    // 🌟 减弱厚度带来的渐隐，防止完全消失
    visibility *= (1.0 - clamp(depth / (thickness * 0.8), 0.0, 1.0));

    // 确保最后可见度在 0 到 1 之间
    visibility = clamp(visibility, 0.0, 1.0);

    // 组合输出：RG存UV坐标，B存可见度
    // 教程写的 uv.ba = vec2(visibility); 意思一样，但我这样写更直观清晰
    FragColor = vec4(uv.x, uv.y, visibility, visibility);
}