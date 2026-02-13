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
    vec3 TangentLightPos;
    mat3 TBN;
}vs_out;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

uniform mat4 model;
uniform mat3 NormalMatrix;

uniform vec3 lightPos;
uniform vec3 viewPos;

void main()
{
    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    vs_out.TexCoords = aTexCoords;

    vec3 T = normalize(NormalMatrix * aTangent);
    vec3 B = normalize(NormalMatrix * aBiTangent);
    vec3 N = normalize(NormalMatrix * aNormal);

    T = normalize(T - dot(T, N) * N); // Gram-Schmidt 正交化
    B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    vs_out.TBN = TBN;

    mat3 TBN_inverse = transpose(TBN);

    vs_out.TangentFragPos = TBN_inverse * vs_out.FragPos;
    vs_out.TangentViewPos = TBN_inverse * viewPos;
    vs_out.TangentLightPos = TBN_inverse * lightPos;

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}