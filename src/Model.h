#ifndef MODEL_H
#define MODEL_H

#include <iterator>
#include <future>
#include <stb_image.h>
#include "Mesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// 1. 定义一个结构体，用来装后台线程解压出来的裸数据
struct TextureData {
    unsigned char* pixels;
    int width, height, nrComponents;
    std::string path;
    std::string typeName; // 保存贴图类型 (diffuse, normal 等)
};

// 🌟 [新增] Assimp 矩阵转 GLM 矩阵的内联辅助函数
inline glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& from)
{
    glm::mat4 to;
    // 注意：Assimp 是行主序，GLM 是列主序，所以这里在赋值时发生了转置
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

class Model
{
public:
    Model(const char* path)
    {
        // 🌟 将贴图翻转设置放在构造函数里，避免多线程同时修改 stb 的全局状态导致崩溃
        stbi_set_flip_vertically_on_load(true);
        loadModel(path);
    }

    void Draw(Shader& shader)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].Draw(shader);
        }
    }

    void DrawPBR(const Shader& shader)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].DrawPBR(shader);
        }
    }

    void DrawInstanced(Shader& shader, unsigned int amount)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].DrawInstanced(shader, amount);
        }
    }

    void ConfigureInstancedArray(unsigned int instanceVBO)
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
        meshes[i].SetupInstancedAttributes(instanceVBO);
        }
    }

    // 🌟 [新增] 纯净版绘制
    void DrawGeometryOnly() 
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
        {
            meshes[i].DrawGeometryOnly();
        }
    }

    std::vector<Mesh> meshes;
private:
    std::string directory;
    std::vector<Texture> textures_loaded;

    void loadModel(const std::string& path)
    {
        Assimp::Importer import;
        const aiScene* scene = import.ReadFile(path, 
            aiProcess_Triangulate |           // 保证是三角形
            aiProcess_CalcTangentSpace |      // 算切线 (因为没用 FlipUVs，切线空间绝对正确)
            aiProcess_JoinIdenticalVertices | // 合并顶点
            aiProcess_GenSmoothNormals |
            // 🌟 核心修复 1：强制 Assimp 烘焙 UE5 的 UV 缩放和偏移，解决纹理巨大/错位问题！
            aiProcess_TransformUVCoords
        );

        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cout << "ERROR::ASSIMP::" << import.GetErrorString() << "\n";
            return;
        }
        directory = path.substr(0, path.find_last_of('/'));

        processNode(scene->mRootNode, scene, glm::mat4(1.0f));
    }

    void processNode(aiNode* node, const aiScene* scene, glm::mat4 parentTransform)
    {
        // 计算当前节点的绝对变换矩阵
        glm::mat4 currentTransform = parentTransform * aiMatrix4x4ToGlm(node->mTransformation);

        for(unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, currentTransform));
        }

        for(unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene, currentTransform);
        }
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene, glm::mat4 transform)
    {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<Texture> textures;

        // 🌟 核心：计算用于转换法线的正规矩阵 (Normal Matrix)
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

        for(unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;

            // 🌟🌟🌟 核心修改 1：把位置从局部空间转换到世界空间 🌟🌟🌟
            glm::vec4 worldPos = transform * glm::vec4(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
            vertex.Position = glm::vec3(worldPos);

            // 🌟🌟🌟 核心修改 2：转换法线朝向 🌟🌟🌟
            glm::vec3 worldNormal = normalMatrix * glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            vertex.Normal = glm::normalize(worldNormal);

            // UV 坐标不受空间变换影响，直接照抄
            if(mesh->mTextureCoords[0])
            {
                vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            }
            else
            {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }

            // 🌟🌟🌟 核心修改 3：转换切线和副切线 🌟🌟🌟
            if (mesh->HasTangentsAndBitangents())
            {
                glm::vec3 worldTangent = normalMatrix * glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                vertex.Tangent = glm::normalize(worldTangent);

                glm::vec3 worldBitangent = normalMatrix * glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                vertex.Bitangent = glm::normalize(worldBitangent);
            }
            else
            {
                vertex.Tangent = glm::vec3(0.0f);
                vertex.Bitangent = glm::vec3(0.0f);
            }

            vertices.push_back(vertex);
        }

        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
            {
                indices.push_back(face.mIndices[j]);
            }
        }

        if(mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            // 1. 漫反射
            std::vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
            textures.insert(textures.end(), std::make_move_iterator(diffuseMaps.begin()), std::make_move_iterator(diffuseMaps.end()));
            // 2. 镜面光
            std::vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
            textures.insert(textures.end(), std::make_move_iterator(specularMaps.begin()), std::make_move_iterator(specularMaps.end()));
            // 3. // 尝试加载 Ambient
            std::vector<Texture> reflectionMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_reflection");
            textures.insert(textures.end(), std::make_move_iterator(reflectionMaps.begin()), std::make_move_iterator(reflectionMaps.end()));
            // ✅ 修改点 6：加载法线贴图
            // .obj 文件通常把法线贴图存在 HEIGHT 属性里，所以我们两个都查一下
            std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normal");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
            
            std::vector<Texture> heightMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
            textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
        }

        return Mesh(vertices, indices, textures);
    }

    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName)
    {
        std::vector<Texture> textures;
        // 准备一个存放“未来结果”的数组
        std::vector<std::future<TextureData>> futureTextures;

        // 【第一阶段：主线程派发任务】
        for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
            std::string pathStr = str.C_Str();
            bool skip = false;
            
            // 1. 检查全局缓存，防止重复加载相同的贴图
            for(unsigned int j = 0; j < textures_loaded.size(); j++)
            {
                if(textures_loaded[j].path == pathStr)
                {
                    // 必须拷贝一份新的，并强行改写类型！绝对不能用以前的类型！
                    Texture cachedTex = textures_loaded[j];
                    cachedTex.type = typeName; 
                    
                    textures.push_back(cachedTex); 
                    skip = true; 
                    break;
                }
            }
            if(skip) continue;

            // 2. 拼接完整的硬盘路径
            std::string fullPath = directory + '/' + pathStr;

            // 🌟 核心魔法：开启后台线程去读硬盘和解压 PNG！
            // std::launch::async 会立刻在线程池里分配一个空闲核来执行这段 Lambda 代码
            futureTextures.push_back(std::async(std::launch::async, [fullPath, pathStr, typeName]() {
                TextureData data;
                data.path = pathStr;
                data.typeName = typeName;
                
                std::cout << "[后台线程] 正在解压贴图: " << fullPath << " ..." << std::endl;
                // 在后台极速解压，主线程完全不被阻塞！
                data.pixels = stbi_load(fullPath.c_str(), &data.width, &data.height, &data.nrComponents, 4);
                return data; 
            }));
        }

        // 【第二阶段：主线程回收结果并生成 OpenGL 纹理】
        // ⚠️ 极其重要：OpenGL 的纹理生成 (glGenTextures) 绝对不能在子线程调用！必须回到主线程！
        for(auto& fut : futureTextures)
        {
            // fut.get() 会等待对应的子线程解压完毕，并拿出数据
            TextureData data = fut.get(); 

            Texture texture;
            texture.type = data.typeName;
            texture.path = data.path;

            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);

            if(data.pixels)
            {
                // GLenum format = GL_RED;
                // if(data.nrComponents == 1) format = GL_RED;
                // else if(data.nrComponents == 3) format = GL_RGB;
                // else if(data.nrComponents == 4) format = GL_RGBA;

                // 🌟 核心修改 4：因为我们强行用了 4 通道加载，格式直接锁死为 GL_RGBA！
                GLenum format = GL_RGBA;

                // 强制对齐为 1（双重保险，虽然 4 通道已经完美对齐了）
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glTexImage2D(GL_TEXTURE_2D, 0, format, data.width, data.height, 0, format, GL_UNSIGNED_BYTE, data.pixels);
                glGenerateMipmap(GL_TEXTURE_2D);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                stbi_image_free(data.pixels); // 释放内存
            }
            else
            {
                // 🚨 保命措施：如果读取失败，塞入 1x1 的洋红色像素，防止黑屏崩溃
                std::cout << "[主线程] 警告: 贴图加载失败 -> " << data.path << std::endl;
                unsigned char errorColor[] = { 255, 0, 255 }; 
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, errorColor);
                
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }

            textures.push_back(texture);
            textures_loaded.push_back(texture); // 存入缓存，下次别的网格要用就不用再读硬盘了
        }
        return textures;
    }
};

#endif