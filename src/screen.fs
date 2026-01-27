#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;

uniform float offset_x;
uniform float offset_y;

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

    // float kernel[9] = float[](
    //     -1, -1, -1,
    //     -1, 9, -1,
    //     -1, -1, -1
    // );

    float kernel[9] = float[](
    1.0 / 16, 2.0 / 16, 1.0 / 16,
    2.0 / 16, 4.0 / 16, 2.0 / 16,
    1.0 / 16, 2.0 / 16, 1.0 / 16  
);

    // float kernel[9] = float[](
    //     1, 1, 1,
    //     1, -8, 1,
    //     1, 1, 1
    // );


    vec3 col = vec3(0.0);
    for(int i = 0; i < 9; i++)
    {
        col += vec3(texture(screenTexture, TexCoords.st + offsets[i])) * kernel[i];
    }

    FragColor = vec4(col, 1.0);
}