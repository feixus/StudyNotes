#ifndef __SHADER__
#define __SHADER__

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

// Creates a new shader program.
class Shader
{
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

public:
    // Sets the vertex shader of this program.
    bool setVertexShader(std::string a_vertexShaderSource);
    bool setFragmentShader(std::string a_fragmentShaderSource);
    bool setTessellationControlShader(std::string a_pathToShaderSourceFile);
    bool setTessellationEvalutionShader(std::string a_pathToShaderSourceFile);
    bool setGeometryShader(std::string a_pathToShaderSourceFile);

    int getUniformLocation(std::string a_uniformName);
    int compile();
    void use();
    Shader(std::string a_vertexShaderSource, std::string a_fragmentShaderSource, std::string a_tcsShaderSource = "", std::string a_tesShaderSource = "",  std::string a_gsShaderSource = "");

private:
    std::string loadShaderSource(std::string a_pathToShaderSourceFile);
    bool shaderCompiled(unsigned int a_id);
};

#endif