#version 330 core
out vec4 FragColor;

in vec3 ourColor;
in vec2 TexCoord; // 接收 UV 坐标

// 采样器 (代表那张图片)
uniform sampler2D texture1;

void main()
{
    // texture(图片, 坐标) -> 返回该坐标下的颜色
    FragColor = texture(texture1, TexCoord);
    
    // 如果想让纹理和顶点颜色混合，用下面这行：
    // FragColor = texture(texture1, TexCoord) * vec4(ourColor, 1.0);
}