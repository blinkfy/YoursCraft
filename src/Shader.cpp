#include "../include/Common.h"
#include "../include/Shader.h"

void Shader::use() { 
    glUseProgram(ID);
}
void Shader::setMat4(const std::string& name,const glm::mat4& mat) { 
    glUniformMatrix4fv(glGetUniformLocation(ID,name.c_str()),1,GL_FALSE,&mat[0][0]);
}
void Shader::setVec3(const std::string& name,const glm::vec3& value) {
    glUniform3fv(glGetUniformLocation(ID,name.c_str()),1,&value[0]);
}
void Shader::setMat3(const std::string& name,const glm::mat3& mat) {
    glUniformMatrix3fv(glGetUniformLocation(ID,name.c_str()),1,GL_FALSE,&mat[0][0]);
}
void Shader::setMat2(const std::string& name,const glm::mat2& mat) {
    glUniformMatrix2fv(glGetUniformLocation(ID,name.c_str()),1,GL_FALSE,&mat[0][0]);
}
void Shader::setVec2(const std::string& name,const glm::vec2& value) {
    glUniform2fv(glGetUniformLocation(ID,name.c_str()),1,&value[0]);
}
void Shader::setFloat(const std::string& name,float value) { 
    glUniform1f(glGetUniformLocation(ID,name.c_str()),value);
}
void Shader::setInt(const std::string& name,int value) { 
    glUniform1i(glGetUniformLocation(ID,name.c_str()),value);
}

void Shader::compile(const char* vertexCode,const char* fragmentCode) {
    unsigned int vertex,fragment;
    int success;char infoLog[512];
    vertex=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex,1,&vertexCode,NULL);
    glCompileShader(vertex);
    glGetShaderiv(vertex,GL_COMPILE_STATUS,&success);
    if(!success) { 
        glGetShaderInfoLog(vertex,512,NULL,infoLog);
        std::cout<<"Vertex shader error:\n"<<infoLog<<std::endl;
    }

    fragment=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment,1,&fragmentCode,NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment,GL_COMPILE_STATUS,&success);
    if(!success) { 
        glGetShaderInfoLog(fragment,512,NULL,infoLog);
        std::cout<<"Fragment shader error:\n"<<infoLog<<std::endl;
    }

    ID=glCreateProgram();
    glAttachShader(ID,vertex);
    glAttachShader(ID,fragment);
    glLinkProgram(ID);
    glGetProgramiv(ID,GL_LINK_STATUS,&success);
    if(!success) { 
        glGetProgramInfoLog(ID,512,NULL,infoLog);
        std::cout<<"Shader link error:\n"<<infoLog<<std::endl;
    }
    glDeleteShader(vertex);glDeleteShader(fragment);
}
