#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

class Shader
{
    public:
    unsigned int ProgramID;

    // 1. 新增：无参默认构造函数 (非常重要，否则无法在类中直接声明)
    Shader() : ProgramID(0) {}

    // 2. 新增：禁用拷贝构造和拷贝赋值 (防止误复制导致多次 glDeleteProgram)
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // 3. 新增：移动构造函数 (掏空临时对象)
    Shader(Shader&& other) noexcept : ProgramID(other.ProgramID) 
    {
        other.ProgramID = 0; // 把原来的 ID 清零，防止被析构函数删掉
    }

    // 4. 新增：移动赋值运算符
    Shader& operator=(Shader&& other) noexcept 
    {
        if (this != &other) 
        {
            // 如果自己本来就有 Shader，先删掉旧的
            if (ProgramID != 0) glDeleteProgram(ProgramID); 
            
            // 夺取新 ID
            ProgramID = other.ProgramID;
            other.ProgramID = 0; // 掏空来源对象
        }
        return *this;
    }

    void destroy()
    {
        release();
    }

    // 构造函数读取并构建着色器
    Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath = nullptr)
    {
        std::string vertexCode;
        std::string fragmentCode;
        std::string geometryCode;

        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        std::ifstream gShaderFile;

        vShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
        gShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);

        try
        {
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);

            std::stringstream vShaderStream, fShaderStream;

            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();

            vShaderFile.close();
            fShaderFile.close();

            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();

            if(geometryPath != nullptr)
            {
                gShaderFile.open(geometryPath);
                std::stringstream gShaderStream;
                gShaderStream << gShaderFile.rdbuf();
                gShaderFile.close();
                geometryCode = gShaderStream.str();
            }
        }
        catch(std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << e.what() << "\n";
        }
        const char* vShaderSource = vertexCode.c_str();
        const char* fShaderSource = fragmentCode.c_str();

        unsigned int vertexShader, fragmentShader;

        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vShaderSource, NULL);
        glCompileShader(vertexShader);
        checkCompileErrors(vertexShader, "VERTEX");

        fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fShaderSource, NULL);
        glCompileShader(fragmentShader);
        checkCompileErrors(fragmentShader, "FRAGMENT");

        unsigned int geometryShader;
        if(geometryPath != nullptr)
        {
            const char* gShaderSource = geometryCode.c_str();
            geometryShader = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(geometryShader, 1, &gShaderSource, NULL);
            glCompileShader(geometryShader);
            checkCompileErrors(geometryShader, "GEOMETRY");
        }

        ProgramID = glCreateProgram();

        glAttachShader(ProgramID, vertexShader);
        glAttachShader(ProgramID, fragmentShader);

        if(geometryPath != nullptr)
        {
            glAttachShader(ProgramID, geometryShader);
        }

        glLinkProgram(ProgramID);
        checkCompileErrors(ProgramID, "PROGRAM");

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        if(geometryPath != nullptr)
        {
            glDeleteShader(geometryShader);
        }
    }

    ~Shader()
    {
        if (ProgramID != 0) 
        {
            glDeleteProgram(ProgramID);
        }
    }

    void use() const
    {
        glUseProgram(ProgramID);
    }

    void Deactivate()
    {
        glUseProgram(0);
    }

    void setBool(std::string name, bool value) const
    {
        glUniform1i(glGetUniformLocation(ProgramID, name.c_str()), (int)value);
    }

    void setInt(std::string name, int value) const{
        glUniform1i(glGetUniformLocation(ProgramID, name.c_str()), value);
    }

    void setFloat(std::string name, float value) const
    {
        glUniform1f(glGetUniformLocation(ProgramID, name.c_str()), value);
    }

    void setMat4(std::string name, const glm::mat4 &mat) const
    {
        glUniformMatrix4fv(glGetUniformLocation(ProgramID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    void setMat3(std::string name, const glm::mat3 &mat) const
    {
        glUniformMatrix3fv(glGetUniformLocation(ProgramID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    void setVec3(std::string name, float x, float y, float z) const
    {
        glUniform3f(glGetUniformLocation(ProgramID, name.c_str()), x, y, z);
    }

    void setVec3(const std::string &name, const glm::vec3 &value) const
    { 
        glUniform3fv(glGetUniformLocation(ProgramID, name.c_str()), 1, &value[0]); 
    }

    void setVec3s(const std::string &name, const std::vector<glm::vec3> &values) const
    {
        glUniform3fv(glGetUniformLocation(ProgramID, name.c_str()), static_cast<GLsizei>(values.size()), &values[0][0]); 
    }

    void setVec2(const std::string &name, const glm::vec2 &value) const
    { 
        glUniform2fv(glGetUniformLocation(ProgramID, name.c_str()), 1, &value[0]); 
    }

    void setMat4s(std::string name, const std::vector<glm::mat4> &matrices) const
    {
        glUniformMatrix4fv(glGetUniformLocation(ProgramID, name.c_str()), static_cast<GLsizei>(matrices.size()), GL_FALSE, (const GLfloat*)matrices.data());
    }

    private:
    void release()
    {
        if (ProgramID != 0)
        {
            glDeleteProgram(ProgramID);
            ProgramID = 0;
        }
    }

    void checkCompileErrors(unsigned int shader, std::string type)
    {
        int success;
        char infoLog[1024];
        if(type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if(!success)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cout<<"ERROR::SHADER_COMPILATION_ERROR of type: "<<type<<"\n"<<infoLog<<"\n -- --------------------------------------------------- -- "<<std::endl;
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if(!success)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cout<<"ERROR::PROGRAM_LINKING_ERROR of type: "<<type<<"\n"<<infoLog<<"\n -- --------------------------------------------------- -- "<<std::endl;
            }
        }
    }
};

#endif