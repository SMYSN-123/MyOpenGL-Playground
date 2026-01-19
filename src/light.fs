#version 330 core

out vec4 FragmentColor;

uniform vec3 lightColor;

void main()
{
    FragmentColor = vec4(lightColor, 1.0);
}