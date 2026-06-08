#ifndef MESH_H
#define MESH_H

#include <vector>

#include "Shader.h"
#include "FrustumCulling.h" // 引入包围盒定义

struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;   // ✅ 新增：切线
    glm::vec3 Bitangent; // ✅ 新增：副切线
};

struct Texture
{
    unsigned int id;
    std::string type;
    std::string path;
};

class Mesh
{
public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    // 🌟 新增：网格专属的包围盒
    AABB boundingBox;

    // 🌟 修改：构造函数接收计算好的 AABB
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures, AABB box)
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;
        this->boundingBox = box; // 保存下来

        setupMesh();
    }

    // Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures)
    // {
    //     this->vertices = vertices;
    //     this->indices = indices;
    //     this->textures = textures;

    //     setupMesh();
    // }

    void Draw(const Shader& shader)
    {
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int reflectionNr = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);

            std::string number;
            std::string name = textures[i].type;

            if(name == "texture_diffuse")
                number = std::to_string(diffuseNr++);
            else if(name == "texture_specular")
                number = std::to_string(specularNr++);
            else if(name == "texture_reflection")
                number = std::to_string(reflectionNr++);

            shader.setInt(("material." + name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void DrawPBR(const Shader& shader) const
    {
        bool hasDiffuse = false;
        bool hasNormal = false;
        
        // 自动桥接 Assimp 读取出来的贴图，喂给我们的 G-Buffer Shader
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            std::string name = textures[i].type;
            
            if(name == "texture_diffuse") {
                shader.setInt("albedoMap", i);
                shader.setBool("useAlbedoMap", true);
                hasDiffuse = true;
            }
            else if(name == "texture_normal" || name == "texture_height") {
                shader.setInt("normalMap", i);
                shader.setBool("useNormalMap", true);
                hasNormal = true;
            }
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        
        // 如果模型缺少贴图，给它一个安全的默认值防止黑屏
        if(!hasDiffuse) {
            shader.setBool("useAlbedoMap", false);
            shader.setVec3("albedoValue", glm::vec3(0.5f)); // 默认灰色
        }
        if(!hasNormal) {
            shader.setBool("useNormalMap", false);
        }
        
        // 关闭特殊的 ORM 贴图，使用模型自带的标准材质
        shader.setBool("usePackedMap", false);
        shader.setBool("useMetalMap", false);
        shader.setFloat("metalValue", 0.0f);
        shader.setBool("useRoughnessMap", false);
        shader.setFloat("roughnessValue", 0.7f); // 默认粗糙一点
        shader.setBool("useAOMap", false);
        shader.setFloat("aoValue", 1.0f);
        shader.setVec2("uvTiling", glm::vec2(1.0f, 1.0f)); // 模型自带 UV 不缩放
        
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

    // 同时，你还需要一个支持实例化的绘制函数
    void DrawInstanced(Shader& shader, unsigned int amount)
    {
        // 绑定纹理部分 (直接复制 Draw 函数里的纹理绑定代码过来)
        // ... (省略纹理绑定代码，和 Draw 一模一样) ...
        unsigned int diffuseNr = 1;
        unsigned int specularNr = 1;
        unsigned int reflectionNr = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            std::string number;
            std::string name = textures[i].type;
            if(name == "texture_diffuse") number = std::to_string(diffuseNr++);
            else if(name == "texture_specular") number = std::to_string(specularNr++);
            else if(name == "texture_reflection") number = std::to_string(reflectionNr++);

            shader.setInt(("material." + name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        glActiveTexture(GL_TEXTURE0);

        // 绘制
        glBindVertexArray(VAO);
        // ✅ 关键：使用 Instanced 版本
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0, amount);
        glBindVertexArray(0);
    }

    // 专门为这个 Mesh 配置实例化矩阵属性 (Location 5, 6, 7, 8)
    void SetupInstancedAttributes(unsigned int instanceVBO)
    {
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

        // mat4 占用 4 个 vec4 插槽
        size_t vec4Size = sizeof(glm::vec4);

        // ✅ 关键修复：显式将其转换为 GLsizei (32位)，消除“可能丢失数据”的警告
        // 因为我们知道 stride (64字节) 肯定放得进 32位整数里
        GLsizei stride = static_cast<GLsizei>(4 * vec4Size);

        // Loc 5
        glEnableVertexAttribArray(5); 
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
        // Loc 6
        glEnableVertexAttribArray(6); 
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, (void*)(1 * vec4Size));
        // Loc 7
        glEnableVertexAttribArray(7); 
        glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * vec4Size));
        // Loc 8
        glEnableVertexAttribArray(8); 
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * vec4Size));

        // 设置除数 (实例化关键)
        glVertexAttribDivisor(5, 1);
        glVertexAttribDivisor(6, 1);
        glVertexAttribDivisor(7, 1);    
        glVertexAttribDivisor(8, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // 🌟 [新增] 纯净版绘制：只负责画几何体，不碰任何贴图！
    void DrawGeometryOnly() const
    {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
private:
    unsigned int VAO, VBO, EBO;

    void setupMesh()
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);

        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(2);

        // ✅ 3: 切线 (Tangent)
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(3);
        // ✅ 4: 副切线 (Bitangent)
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        glEnableVertexAttribArray(4);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
};

#endif