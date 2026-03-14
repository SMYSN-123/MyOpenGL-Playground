#version 330 core
out vec4 FragColor;

in vec3 localPos;

uniform samplerCube environmentMap;

void main()
{
    vec3 envColor = texture(environmentMap, localPos).rgb;
    // vec3 envColor = textureLod(environmentMap, localPos, 1.2).rgb;

    FragColor = vec4(envColor, 1.0);
}