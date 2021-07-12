// System libs.
#include <iostream>

// Include libs.
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shader.h"

#define M_PI 3.14159

// Header includes.
// TODO: Include your headers here...

// Function prototypes.
void WindowResize(GLFWwindow* a_window, int a_width, int a_height);

// Classes.
class Engine
{
public:
    int Initialize();
    float gameSpeed = 0.1f;

private:
    int screenWidth = 800;
    int screenHeight = 600;

    const char* windowName = "Default Name";

    GLFWwindow* window;

    // Game loop.
    float lastFrameTime = 0.0f;
    glm::vec3 clearColor = glm::vec3(0.0f, 0.0f, 0.0f);

public:
    Engine(int a_width, int a_height, const char* a_windowName);

    void Update(float a_deltaTime);
    void Draw();
    void ShutDown();

    void ProcessInput(GLFWwindow* a_window);

private:
    // OpenGL
    void SetupOpenGlRendering();
    
    Shader* basicShader;
    unsigned int VAO, VBO;

    void CreateACube();
};