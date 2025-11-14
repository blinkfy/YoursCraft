#pragma once
#include "Common.h"

// Shader ·â×°ÉùÃ÷
class Shader {
public:
    unsigned int ID;
    void use();
    void setMat4(const std::string& name,const glm::mat4& mat);
    void setVec3(const std::string& name,const glm::vec3& value);
    void setMat3(const std::string& name,const glm::mat3& mat);
    void setMat2(const std::string& name,const glm::mat2& mat);
    void setVec2(const std::string& name,const glm::vec2& value);
    void setFloat(const std::string& name,float value);
    void setInt(const std::string& name,int value);
    void compile(const char* vertexCode,const char* fragmentCode);
};

