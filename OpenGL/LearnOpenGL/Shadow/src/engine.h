#ifndef __ENGINE__
#define __ENGINE__

// System libs.
#include <iostream>
#include <vector>

// Include libs.
#include <glad\glad.h>
#include <GLFW\glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shader.h"
#include "camera.h"
#include "common.h" 

#define M_PI 3.14159

// Header includes.
// TODO: Include your headers here...

// Function prototypes.
void framebuffer_size_callback(GLFWwindow* a_window, int a_width, int a_height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

const unsigned int SCR_WIDTH = 1334;
const unsigned int SCR_HEIGHT = 750;

const unsigned int SHADOW_WIDTH = 1024;
const unsigned int SHADOW_HEIGHT = 1024;

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

    const char* windowName = "Default Name";

    GLFWwindow* window;

    // Game loop.
    float lastFrameTime = 0.0f;
    float deltaTime = 0.0f;

    Common commonLib;
      
    unsigned int cubeVAO = 0, cubeVBO;
    unsigned int quadVAO = 0, quadVBO;
    unsigned int sphereVAO = 0, sphereVBO, sphereEBO;
    unsigned int planeVAO = 0, planeVBO;

    unsigned int planeTexture;

    unsigned int depthMapFBO;
    unsigned int depthMap;

    glm::vec3 lightPos;

    Shader* simpleDepthShader;
    Shader* debugQuadShader;

public:
    Engine(int a_width, int a_height, const char* a_windowName);

    int Initialize();

    void Draw();
    void ShutDown();

    void ProcessInput(GLFWwindow* a_window);

private:
    void SetupOpenGlRendering();

    void RenderScene(const Shader* shader);
    void RenderPlane(unsigned int& vao, unsigned int& vbo);
};

#endif