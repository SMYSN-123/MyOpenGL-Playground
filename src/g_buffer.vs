#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBiTangent;

out VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 TangentLightDir;
    mat3 TBN;
}vs_out;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

uniform mat4 model;
uniform mat3 NormalMatrix;

uniform vec3 lightDir;
uniform vec3 viewPos;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    vs_out.TexCoords = aTexCoords;

    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = normalize(NormalMatrix * aBiTangent);
    vec3 N = normalize(NormalMatrix * aNormal);

    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);
    vs_out.TBN = mat3(T, B, N);

    mat3 invTBN = transpose(vs_out.TBN);

    vs_out.TangentViewPos  = invTBN * viewPos;
    vs_out.TangentFragPos  = invTBN * vs_out.FragPos;
    vs_out.TangentLightDir = invTBN * lightDir;
}