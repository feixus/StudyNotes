#ifndef __ENGINE__
#define __ENGINE__

// System libs.
#include <iostream>
#include <vector>
#include <random>

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

const unsigned int SHADOW_WIDTH = 4096;
const unsigned int SHADOW_HEIGHT = 4096;

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
    unsigned int lightFBO = 0;
    unsigned int lightDepthMaps;

    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 500.0f;

    std::vector<float> shadowCascadeLevels;

    const glm::vec3 lightDir = glm::normalize(glm::vec3(20.0f, 50, 20.0f));

    std::vector<glm::mat4> lightMatricesCache;

    unsigned int matricesUBO;

    std::vector<GLuint> visualizerVAOs;
    std::vector<GLuint> visualizerVBOs;
    std::vector<GLuint> visualizerEBOs;

    int debugLayer = 0;
    bool showQuad = false;

    std::random_device device;
    std::mt19937 generator = std::mt19937(device());

    Shader* simpleDepthShader;
    Shader* debugCascadeShader;
    Shader* debugQuadShader;

public:
    Engine(int a_width, int a_height, const char* a_windowName);

    int Initialize();

    void Draw();
    void ShutDown();

    void ProcessInput(GLFWwindow* a_window);

private:
    void SetupOpenGlRendering();

    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview);
    std::vector<glm::mat4> getLightSpaceMatrices();
    glm::mat4 getLightSpaceMatrix(const float nearPlane, const float farPlane);

    void drawCascadeVolumeVisualizers(const std::vector<glm::mat4>& lightMatrices, Shader* shader);

    void RenderScene(const Shader* shader);
    void RenderPlane(unsigned int& vao, unsigned int& vbo);
};

#endif