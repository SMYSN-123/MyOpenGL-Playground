#version 330 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D ssaoInput;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0)); // 获取纹理的像素大小
    float result = 0.0;
    for(int x = -2; x < 2; x++)
    {
        for(int y = -2; y < 2; y++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize; // 计算偏移
            result += texture(ssaoInput, TexCoords + offset).r; // 累加周围像素的值
        }
    }
    FragColor = result / (4.0 * 4.0); // 取平均值
}