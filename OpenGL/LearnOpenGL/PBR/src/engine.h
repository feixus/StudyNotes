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

static float lastX = SCR_WIDTH / 2.0;
static float lastY = SCR_HEIGHT / 2.0;
 
static float firstMouse = true;
static float deltaTime = 0.0f;
static float lastFrame = 0.0f;

static unsigned int indexCount = 0;

const int nrRows = 7, nrColumns = 7;
const float spacing = 2.5f;

static Camera* camera;

// Classes.
class Engine
{
public:
    int Initialize();
    float gameSpeed = 0.1f;

private:
    int screenWidth = SCR_WIDTH;
    int screenHeight = SCR_HEIGHT;

    const char* windowName = "Default Name";

    GLFWwindow* window;

    // Game loop.
    float lastFrameTime = 0.0f;
    float deltaTime = 0.0f;
    glm::vec3 clearColor = glm::vec3(0.0f, 0.0f, 0.0f);

    glm::vec3 lightPositions[4];
    glm::vec3 lightColors[4];

    //使用纹理化参数的标记,  切换开关
    bool useTextureParameters = true;

    unsigned int textureIds[5][5];
    glm::mat4 textureModels[5];

public:
    Engine(int a_width, int a_height, const char* a_windowName);

    void Draw();
    void ShutDown();

    void ProcessInput(GLFWwindow* a_window);

private:
    // OpenGL
    void SetupOpenGlRendering();

    Common commonLib;
    
    Shader* basicShader;
    unsigned int cubeVAO = 0, cubeVBO, textureID;
    unsigned int QuadVAO = 0, QuadVBO;
    unsigned int SphereVAO = 0, SphereVBO, SphereEBO;
    unsigned int albedo, normal, metallic, roughness, ao;

    unsigned int hdrTexture;
    unsigned int captureFBO, captureRBO;
    unsigned int envCubemap, irradianceMap;

    glm::mat4 captureProjection;
    glm::mat4 captureViews[6];

    //ambient specular 
    unsigned int prefilterEnvmap;
    unsigned int brdfLUTexture;

    Shader* equirectangularToCubemapShader;
    Shader* skyboxShader;
    Shader* irradianceShader;
    Shader* prefilterShader;
    Shader* brdfIntegrationShader;
    Shader* quadShader;

    unsigned int GenerateACubemap(int width, int height, GLint minificationFilter = GL_LINEAR, bool useMipmap = false);

    void GenerateRadianceEnvCubemap();
    void GenerateIrradianceEnvCubemap();
    void GenerateAmbientSpecular();
};

#endif