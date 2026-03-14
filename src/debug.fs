#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D debugTexture;

void main()
{
    // 只读取 R 和 G 通道
    vec2 texColor = texture(debugTexture, TexCoords).rg;
    
    // 把 R 放红，G 放绿，B 填 0，A 填 1
    FragColor = vec4(texColor.r, texColor.g, 0.0, 1.0);
}