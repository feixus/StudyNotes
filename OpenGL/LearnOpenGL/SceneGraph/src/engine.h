#ifndef __ENGINE__
#define __ENGINE__

// System libs.
#include <iostream>
#include <vector>
#include <string>
#include <memory>

// Include libs.
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shader.h"
#include "camera.h"
#include "common.h" 
#include "entity.h"

#define M_PI 3.14159

// Function prototypes.
void framebuffer_size_callback(GLFWwindow* a_window, int a_width, int a_height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

const unsigned int SCR_WIDTH = 1334;
const unsigned int SCR_HEIGHT = 750;

static float lastX = SCR_WIDTH / 2.0;
static float lastY = SCR_HEIGHT / 2.0;
 
static float firstMouse = true;
static Camera* camera;

// Classes.
class Engine
{
public:
    Shader* basicShader;

private:
    int screenWidth = SCR_WIDTH;
    int screenHeight = SCR_HEIGHT;
    const char* windowName;

    GLFWwindow* window;

    // Game loop.
    float lastFrameTime = 0.0f;
    float deltaTime = 0.0f;

    Entity ourEntity;
public:
    Engine(int a_width, int a_height, const char* a_windowName);

    int Initialize();
    void Draw();
    void ShutDown();
    void ProcessInput(GLFWwindow* a_window);

private:
    void SetupOpenGlRendering();
};

#endif