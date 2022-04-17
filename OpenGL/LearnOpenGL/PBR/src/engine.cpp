#include "engine.h"

Engine::Engine(int a_width, int a_height, const char *a_windowName)
{
    this->screenWidth = a_width;
    this->screenHeight = a_height;
    this->windowName = a_windowName;
}

int Engine::Initialize()
{
    // Initialize GLFW.
    glfwInit();

    // Tell GLFW that we want to use OpenGL 3.3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);

    // Tell GLFW that we want to use the OpenGL's core profile.
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    glfwWindowHint(GLFW_SAMPLES, 4);

    // Do this for mac compatability.
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create Window.

    // Instantiate the window object.
    this->window = glfwCreateWindow(this->screenWidth, this->screenHeight, this->windowName, NULL, NULL);

    // Make sure that the window is created.
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window." << std::endl;
        glfwTerminate();

        std::cin.get();
        return 0;
    }

    glfwMakeContextCurrent(window);

    // Initialize GLAD.

    // Make sure that glad has been initialized successfully.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD." << std::endl;

        std::cin.get();
        return 0;
    }

    // Set the viewport
    glViewport(0, 0, this->screenWidth, this->screenHeight);

    // Setup callbacks.
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    //tell GLFW to capture our mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    this->SetupOpenGlRendering();

    // Start game loop.
    while (!glfwWindowShouldClose(this->window))
    {
        // Calculate the elapsed time between the current and previous frame.
        float m_frameTime = (float)glfwGetTime();
        deltaTime = m_frameTime - this->lastFrameTime;
        this->lastFrameTime = m_frameTime;

        this->ProcessInput(this->window);

        this->Draw();

        glfwSwapBuffers(this->window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 1;
}

void framebuffer_size_callback(GLFWwindow *a_window, int a_width, int a_height)
{
    glViewport(0, 0, a_width, a_height);
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if(firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = -xpos + lastX;
    float yoffset = ypos - lastY;

    lastX = xpos;
    lastY = ypos;

    camera->ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    camera->ProcessMouseScroll(static_cast<float>(yoffset));
}

void Engine::ProcessInput(GLFWwindow *a_window)
{
    // If the escape key gets pressed, close the window.
    if (glfwGetKey(a_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(a_window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera->ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera->ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera->ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera->ProcessKeyboard(RIGHT, deltaTime);
}

void Engine::SetupOpenGlRendering()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    camera = new Camera(glm::vec3(0.0f, 0.0f, 23.0f));

    basicShader = new Shader("src/shaders/basic_vs.glsl", "src/shaders/basic_fs.glsl");
    basicShader->use();

    basicShader->setVec3("albedo", 0.5f, 0.0f, 0.0f);
    basicShader->setFloat("ao", 1.0f);

    basicShader->setInt("irradianceMap", 0);
    basicShader->setInt("prefilterMap", 1);
    basicShader->setInt("brdfLUT", 2);

     //lights
    lightPositions[0] = glm::vec3(-10.0f,  10.0f, 10.0f);
    lightPositions[1] = glm::vec3( 10.0f,  10.0f, 10.0f);
    lightPositions[2] = glm::vec3(-10.0f, -10.0f, 10.0f);
    lightPositions[3] = glm::vec3( 10.0f, -10.0f, 10.0f);

    lightColors[0] = glm::vec3(300.0f, 300.0f, 300.0f);
    lightColors[1] = glm::vec3(300.0f, 300.0f, 300.0f);
    lightColors[2] = glm::vec3(300.0f, 300.0f, 300.0f);
    lightColors[3] = glm::vec3(300.0f, 300.0f, 300.0f);

    glm::mat4 projection = glm::perspective(glm::radians(camera->Zoom), (float)screenWidth / (float)screenHeight, 0.1f, 100.0f);
    basicShader->use();
    basicShader->setMat4("projection", projection);

    GenerateRadianceEnvCubemap();
    GenerateIrradianceEnvCubemap();

    GenerateAmbientSpecular();

    skyboxShader = new Shader("src/shaders/skybox_vs.glsl", "src/shaders/skybox_fs.glsl");
    skyboxShader->use();
    skyboxShader->setInt("environmentMap", 0);
    skyboxShader->setMat4("projection", projection);

    int scrWidth, scrHeight;
    glfwGetFramebufferSize(window, &scrWidth, &scrHeight);
    glViewport(0, 0, scrWidth, scrHeight);

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    std::cout << std::endl;
    std::cout << "error code = " << glGetError() << "  -->>>>" << std::endl;
    std::cout << "opengl version: " << glGetString ( GL_SHADING_LANGUAGE_VERSION ) << std::endl;
}

void Engine::Draw()
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    basicShader->use();
    glm::mat4 view = camera->GetViewMatrix();
    basicShader->setMat4("view", view);
    basicShader->setVec3("camPos", camera->Position);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, irradianceMap);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, prefilterEnvmap);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, brdfLUTexture);

    glm::mat4 model = glm::mat4(1.0f);
    for (int row = 0; row < nrRows; ++row)
    {
        basicShader->setFloat("metallic", (float)row / (float)nrRows);
        for (int col = 0; col < nrColumns; ++col)
        {
            //clamp the roughness to 0.05-1.0 as perfectly smooth surface 
            //(roughness 0.0) tend to look a bit off on direct light
            basicShader->setFloat("roughness", glm::clamp((float)col / (float)nrColumns, 0.05f, 1.0f));

            model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3((col - (nrColumns / 2)) * spacing, (row - (nrRows / 2)) * spacing, 0.0f));

            basicShader->setMat4("model", model);
            Common::RenderSphere(SphereVAO, SphereVBO, SphereEBO, indexCount);
        }
    }

    for (unsigned int i = 0; i < sizeof(lightPositions) / sizeof(lightPositions[0]); ++i)
    {
        glm::vec3 newPos = lightPositions[i] + glm::vec3(sin(glfwGetTime() * 5.0) * 5.0, 0.0, 0.0);
        newPos = lightPositions[i];
        basicShader->setVec3("lightPositions[" + std::to_string(i) + "]", newPos);
        basicShader->setVec3("lightColors[" + std::to_string(i) + "]", lightColors[i]);

        model = glm::mat4(1.0f);
        model = glm::translate(model, newPos);
        model = glm::scale(model, glm::vec3(0.5f));
        basicShader->setMat4("model", model);
        Common::RenderSphere(SphereVAO, SphereVBO, SphereEBO, indexCount);
    }

    // brdfIntegrationShader->use();
    // Common::RenderQuad(QuadVAO, QuadVBO);

    //render skybox
    skyboxShader->use();
    skyboxShader->setMat4("view", view);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    // glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    // glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterEnvmap);

    Common::RenderCube(cubeVAO, cubeVBO);
}

unsigned int Engine::GenerateACubemap(int width, int height, GLint minificationFilter, bool useMipmap)
{
    unsigned int cubemap;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    for (unsigned int i = 0; i < 6; i++)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minificationFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (useMipmap)
    {
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        //mip level的低分辨率，而越是粗糙的表面，其specular lobe范围越是大，导cube faces之间的采样不理想
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }

    return cubemap;
}

void Engine::GenerateRadianceEnvCubemap()
{
    equirectangularToCubemapShader = new Shader("src/shaders/cubemap_vs.glsl", "src/shaders/equirectangular_to_cubemap_fs.glsl");

    //setup framebuffer renderBuffer
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    //load HDR environment map
    // hdrTexture = LoadHDRTexture("src/textures/Brooklyn_Bridge_Planks_2k.hdr");
    hdrTexture = Common::LoadHDRTexture("src/textures/HWSign3-Fence_2k.hdr");

    // cubemap
    envCubemap = GenerateACubemap(512, 512);

    captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
       
    captureViews[0] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f));
    captureViews[1] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f));
    captureViews[2] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f));
    captureViews[3] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f));
    captureViews[4] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f));
    captureViews[5] = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f));

    //convert HDR equirectangular environment map to cubemap equivalent
    equirectangularToCubemapShader->use();
    equirectangularToCubemapShader->setInt("equirectangularMap", 0);
    equirectangularToCubemapShader->setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdrTexture);

    glViewport(0, 0, 512, 512);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; i++)
    {
        equirectangularToCubemapShader->setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, envCubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Common::RenderCube(cubeVAO, cubeVBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::GenerateIrradianceEnvCubemap()
{
    int width = 32, height = 32;
    irradianceMap = GenerateACubemap(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    irradianceShader = new Shader("src/shaders/cubemap_vs.glsl", "src/shaders/irradiance_fs.glsl");

    irradianceShader->use();
    irradianceShader->setInt("environmentMap", 0);
    irradianceShader->setMat4("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glViewport(0, 0, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    for (unsigned int i = 0; i < 6; i++)
    {
        irradianceShader->setMat4("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Common::RenderCube(cubeVAO, cubeVBO);    
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

//indirect light: prefilter env cubemap * (brdf integration map)
void Engine::GenerateAmbientSpecular()
{
    //1. prefilter env map 

    //对于大多数reflection，128*128已经够用，但大量光滑的材质(如car reflection)需要提升分辨率
    prefilterEnvmap = GenerateACubemap(128, 128, GL_LINEAR_MIPMAP_LINEAR, true);

    prefilterShader = new Shader("src/shaders/cubemap_vs.glsl", "src/shaders/prefilter_env_map_fs.glsl");
    prefilterShader->use();
    prefilterShader->setInt("environmentMap", 0);
    prefilterShader->setMat4("projection", captureProjection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    //prefilter mipmap levels
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    unsigned int maxMipLevels = 5;
    for (unsigned int mip = 0; mip < maxMipLevels; mip++)
    {
        unsigned int mipWidth = 128 * std::pow(0.5, mip);
        unsigned int mipHeight = 128 * std::pow(0.5, mip);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float)mip / (float)(maxMipLevels - 1);
        prefilterShader->setFloat("roughness", roughness);
        for (unsigned int i = 0; i < 6; i++)
        {
            prefilterShader->setMat4("view", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterEnvmap, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            Common::RenderCube(cubeVAO, cubeVBO);
        }
    }    

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //2. brdf integration LUT 
    glGenTextures(1, &brdfLUTexture);

    //pre-allocate enough memory for the LUT texture
    glBindTexture(GL_TEXTURE_2D, brdfLUTexture);
    //16-bit precision floating point as recommended by Epic Games
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, 0);
    //GL_CLAMP_TO_EDGE prevent sampling artifacts
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUTexture, 0);

    glViewport(0, 0, 512, 512);
    brdfIntegrationShader = new Shader("src/shaders/brdf_integration_vs.glsl", "src/shaders/brdf_integration_fs.glsl");
    brdfIntegrationShader->use();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Common::RenderQuad(QuadVAO, QuadVBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::ShutDown()
{
    delete &basicShader;
}

