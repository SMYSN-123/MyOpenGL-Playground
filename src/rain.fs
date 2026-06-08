#version 430 core
out vec4 FragColor;

in vec3 ParticleColor;
in float ParticleAlpha;

void main()
{
    if (ParticleAlpha <= 0.01) discard; 
    
    // 🌟 调优 6：将极其暴力的 HDR 2.0 倍发光，降温为 1.2 倍
    // 让雨水能被 Bloom 捕获到一点点微光，但绝对不再像激光剑一样刺眼
    FragColor = vec4(ParticleColor * 1.2, ParticleAlpha);
}