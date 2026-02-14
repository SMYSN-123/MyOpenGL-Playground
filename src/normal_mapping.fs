#version 330 core
layout ( location = 0 ) out vec4 FragColor;
layout ( location = 1 ) out vec4 BrightColor;

in VS_OUT
{
    vec3 FragPos;
    vec2 TexCoords;
    mat3 TBN; // 切线空间矩阵
}fs_in;

uniform sampler2D diffuseTexture;
uniform samplerCube shadowMap;
uniform sampler2D normalMap;

uniform vec3 lightPos;
uniform vec3 viewPos;

uniform vec3 lightColor;

uniform float far_plane;

uniform bool blinn;

// ✅ 泊松圆盘采样的预定义向量数组 (硬编码)
// 这些向量指向四面八方，且彼此距离比较均匀
vec3 sampleOffsetDirections[20] = vec3[]
(
    vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
    vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
    vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
    vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
    vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

float ShadowCalculation(vec3 FragPos)
{
    vec3 fragToLight = FragPos - lightPos;
    float currentDepth = length(fragToLight);
    
    float shadow = 0.0;
    float bias = 0.15; // 稍微大一点的 bias
    int samples = 20;  // 采样次数
    
    float viewDistance = length(viewPos - FragPos);
    
    // ✅ 动态半径：距离越远，阴影越软
    // / 25.0 是一个经验参数，你可以调整它来控制模糊程度
    float diskRadius = (1.0 + (viewDistance / far_plane)) / 25.0;
    
    for(int i = 0; i < samples; ++i)
    {
        // 采样：原来的方向 + 泊松偏移 * 半径
        float closestDepth = texture(shadowMap, fragToLight + sampleOffsetDirections[i] * diskRadius).r;
        
        // 还原单位
        closestDepth *= far_plane;   
        
        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }
    
    shadow /= float(samples);

    return shadow;
}

void main()
{
    vec3 color = texture(diffuseTexture, fs_in.TexCoords).rgb;

    // ✅ 从法线贴图中获取切线空间的法线
    vec3 normal = texture(normalMap, fs_in.TexCoords).rgb;
    normal = normal * 2.0 - 1.0; // 从 [0,1] 转换到 [-1,1]
    // FragColor = vec4(normal * 0.5 + 0.5, 1.0);
    normal = normalize(fs_in.TBN * normal); // 转换到世界空间

    // vec3 lightColor = vec3(1.0);
    // Ambient
    vec3 ambient = 0.05 * lightColor;
    // Diffuse
    vec3 lightDir = normalize(lightPos - fs_in.FragPos);
    float diff = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = diff * lightColor;
    // Specular
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    float spec = 0.0;
    if(blinn)
    {
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    }
    else
    {
        vec3 reflectDir = reflect(-lightDir, normal);
        spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0);
    }
    vec3 specular = spec * lightColor;
    // 实现衰弱
    float distance = length(lightPos - fs_in.FragPos);
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    float attenuation = 1.0/(constant + linear * distance + quadratic * distance * distance);
    ambient  *= attenuation; 
    diffuse  *= attenuation;
    specular *= attenuation;
    // Calculate shadow
    float shadow = ShadowCalculation(fs_in.FragPos);
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * color;

    FragColor = vec4(lighting, 1.0);

    float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = FragColor;
    else
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}