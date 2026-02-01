#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out VS_OUT
{
    vec3 normal;
}vs_out;

layout (std140) uniform Matrices
{
    mat4 projection; // 虽然我不用，但我得占着位，保证内存布局对齐
    mat4 view;       // 我只用这个
};

uniform mat4 model;

void main()
{
    gl_Position = view * model * vec4(aPos, 1.0);
    mat3 normalMatrix = mat3(transpose(inverse(view * model)));
    vs_out.normal = normalize(vec3(vec4(normalMatrix * aNormal, 0.0)));
}