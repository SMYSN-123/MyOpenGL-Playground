#version 330 core
out float FragColor;

in vec2 TexCoords;

layout (std140) uniform Matrices
{
    mat4 projection;
    mat4 view;
};

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];

uniform float radius;

const vec2 noiseScale = vec2(800.0/4.0, 600.0/4.0); // 根据屏幕尺寸调整噪声纹理的缩放

void main()
{
    vec3 fragPos_world = texture(gPosition, TexCoords).xyz;
    vec3 normal_world = texture(gNormal, TexCoords).rgb;
    vec3 randomVec = texture(texNoise, TexCoords * noiseScale).xyz;

    vec3 fragPos_view = vec3(view * vec4(fragPos_world, 1.0));
    vec3 normal_view = normalize(mat3(view) * normal_world);

    vec3 tangent = normalize(randomVec - normal_view * dot(randomVec, normal_view));
    vec3 bitangent = cross(normal_view, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal_view);

    float occlusion = 0.0;
    for(int i = 0; i < 64; i++)
    {
        vec3 sample = TBN * samples[i];
        sample = fragPos_view + sample * radius;

        vec4 offset = vec4(sample, 1.0);
        offset = projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        vec3 samplePos_world = texture(gPosition, offset.xy).xyz;
        float sampleDepth = (view * vec4(samplePos_world, 1.0)).z;

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos_view.z - sampleDepth)); // 根据深度差异计算权重
        occlusion += (sampleDepth >= sample.z + 0.025 ? 1.0 : 0.0) * rangeCheck; // 如果样本点被遮挡，增加遮蔽值
    }
    occlusion = 1.0 - (occlusion / 64.0); // 归一化遮蔽值
    FragColor = occlusion;
}