#ifndef __SHADER__
#define __SHADER__

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Creates a new shader program.
class Shader
{
public:
    Shader(const char* a_vertexShaderSource, const char* a_fragmentShaderSource);
    Shader(const char* a_vertexShaderSource, const char* a_fragmentShaderSource, const char* a_tcsShaderSource, const char* a_tesShaderSource,  const char* a_gsShaderSource);
    
    // Sets the vertex shader of this program.
    bool setVertexShader(std::string a_vertexShaderSource);
    bool setFragmentShader(std::string a_fragmentShaderSource);
    bool setTessellationControlShader(std::string a_pathToShaderSourceFile);
    bool setTessellationEvalutionShader(std::string a_pathToShaderSourceFile);
    bool setGeometryShader(std::string a_pathToShaderSourceFile);

    int getUniformLocation(std::string a_uniformName);
    int compile();
    void use();

    void setMat4(const std::string &name, const glm::mat4 &mat) const;
    void setVec3(const std::string &name, const glm::vec3 &vector) const;
    void setFloat(const std::string &name, float variable) const;
    void setInt(const std::string &name, int variable) const;
    
private:
    unsigned int shaderProgram;

    unsigned int vertexShaderID = 0;
    const char* vertexShaderSource;

    unsigned int fragmentShaderID = 0;
    const char* fragmentShaderSource;

    unsigned int tesShaderID = 0;
    const char* tesShaderSource;

    unsigned int tcsShaderID = 0;
    const char* tcsShaderSource;

    unsigned int gsShaderID = 0;
    const char* gsShaderSource; 

    std::string loadShaderSource(std::string a_pathToShaderSourceFile);
    bool shaderCompiled(unsigned int a_id);
};

#endif