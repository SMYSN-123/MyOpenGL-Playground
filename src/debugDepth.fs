#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D depthMap;
uniform float near_plane;
uniform float far_plane;

// 线性化深度：把非线性的 [0, 1] 深度值还原回 View Space 的 z 值
float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; // Back to NDC 
    return (2.0 * near_plane * far_plane) / (far_plane + near_plane - z * (far_plane - near_plane));	
}

void main()
{             
    float depthValue = texture(depthMap, TexCoords).r;
    
    // 🅰️ 如果光照是 透视投影 (Perspective) -> 使用线性化
    // 除以 far_plane 是为了把巨大的距离值(如 50.0) 映射回 [0, 1] 区间以便显示
    // FragColor = vec4(vec3(LinearizeDepth(depthValue) / far_plane), 1.0); 

    // 🅱️ 如果光照是 正交投影 (Orthographic) -> 直接输出
    FragColor = vec4(vec3(depthValue), 1.0); 
}