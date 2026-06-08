#ifndef COMPUTE_SHADER_H
#define COMPUTE_SHADER_H

#include <glad/glad.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

class ComputeShader
{
    public:
    unsigned int ProgramID;

    // 1. 新增：无参默认构造函数 (非常重要，否则无法在类中直接声明)
    ComputeShader() : ProgramID(0) {}

    // 2. 新增：禁用拷贝构造和拷贝赋值 (防止误复制导致多次 glDeleteProgram)
    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;

    // 3. 新增：移动构造函数 (掏空临时对象)
    ComputeShader(ComputeShader&& other) noexcept : ProgramID(other.ProgramID) 
    {
        other.ProgramID = 0; // 把原来的 ID 清零，防止被析构函数删掉
    }

    // 4. 新增：移动赋值运算符
    ComputeShader& operator=(ComputeShader&& other) noexcept 
    {
        if (this != &other) 
        {
            // 如果自己本来就有 ComputeShader，先删掉旧的
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
    ComputeShader(const char* computePath)
    {
        std::string computeCode;

        std::ifstream cShaderFile;

        cShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);

        try
        {
            cShaderFile.open(computePath);

            std::stringstream cShaderStream;

            cShaderStream << cShaderFile.rdbuf();

            cShaderFile.close();

            computeCode = cShaderStream.str();
        }
        catch(std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << e.what() << "\n";
        }
        const char* cShaderSource = computeCode.c_str();

        unsigned int computeShader;

        computeShader = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(computeShader, 1, &cShaderSource, NULL);
        glCompileShader(computeShader);
        checkCompileErrors(computeShader, "COMPUTE");

        ProgramID = glCreateProgram();

        glAttachShader(ProgramID, computeShader);

        glLinkProgram(ProgramID);
        checkCompileErrors(ProgramID, "PROGRAM");

        glDeleteShader(computeShader);
    }

    ~ComputeShader()
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

    void checkCompileErrors(unsigned int ComputeShader, std::string type)
    {
        int success;
        char infoLog[1024];
        if(type != "PROGRAM")
        {
            glGetShaderiv(ComputeShader, GL_COMPILE_STATUS, &success);
            if(!success)
            {
                glGetShaderInfoLog(ComputeShader, 1024, NULL, infoLog);
                std::cout<<"ERROR::SHADER_COMPILATION_ERROR of type: "<<type<<"\n"<<infoLog<<"\n -- --------------------------------------------------- -- "<<std::endl;
            }
        }
        else
        {
            glGetProgramiv(ComputeShader, GL_LINK_STATUS, &success);
            if(!success)
            {
                glGetProgramInfoLog(ComputeShader, 1024, NULL, infoLog);
                std::cout<<"ERROR::PROGRAM_LINKING_ERROR of type: "<<type<<"\n"<<infoLog<<"\n -- --------------------------------------------------- -- "<<std::endl;
            }
        }
    }
};

#endif