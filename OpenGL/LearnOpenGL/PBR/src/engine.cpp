#include "engine.h"

//You must not #define STB_IMAGE_IMPLEMENTATION in header (.h) files.  
//Only in one C/C++ file to create the implementation (the stuff that has to be unique and done only once.)
//https://gamedev.stackexchange.com/questions/158106/why-am-i-getting-these-errors-when-including-stb-image-h
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

        // Application logic
        // this->Update(deltaTime);
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

    // TODO: Do your resize logic here...
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

    stbi_set_flip_vertically_on_load(true);

    camera = new Camera(glm::vec3(0.0f, 0.0f, 23.0f));
    basicShader = new Shader("src/shaders/basic_vs.glsl", "src/shaders/basic_fs.glsl");
    basicShader->use();
    basicShader->setInt("albedoMap", 0);
    basicShader->setInt("normalMap", 1);
    basicShader->setInt("metallicMap", 2);
    basicShader->setInt("roughnessMap", 3);
    basicShader->setInt("aoMap", 4);

    basicShader->setVec3("albedo", 0.5f, 0.0f, 0.0f);
    basicShader->setFloat("ao", 1.0f);

    albedo = LoadTexture("src/textures/metal/albedo.png");
    normal = LoadTexture("src/textures/metal/normal.png");
    metallic = LoadTexture("src/textures/metal/metallic.png");
    roughness = LoadTexture("src/textures/metal/roughness.png");
    ao = LoadTexture("src/textures/metal/ao.png");

    // glBindTextureUnit(0, albedo);
    // glBindTextureUnit(1, normal);
    // glBindTextureUnit(2, metallic);
    // glBindTextureUnit(3, roughness);
    // glBindTextureUnit(4, ao);

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

    skyboxShader = new Shader("src/shaders/skybox_vs.glsl", "src/shaders/skybox_fs.glsl");
    skyboxShader->use();
    skyboxShader->setInt("environmentMap", 0);
    skyboxShader->setMat4("projection", projection);

    int scrWidth, scrHeight;
    glfwGetFramebufferSize(window, &scrWidth, &scrHeight);
    glViewport(0, 0, scrWidth, scrHeight);

    std::cout << "error code = " << glGetError() << "  -->>>>" << std::endl;
    std::cout << glGetString ( GL_SHADING_LANGUAGE_VERSION ) << std::endl;

    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

void Engine::Update(float a_deltaTime)
{
    // TODO: Update your logic here...
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
    glBindTexture(GL_TEXTURE_2D, albedo);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, metallic);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, roughness);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, ao);

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

    //render skybox
    skyboxShader->use();
    skyboxShader->setMat4("view", view);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    // glBindTexture(GL_TEXTURE_CUBE_MAP, irradianceMap);
    Common::RenderCube(cubeVAO, cubeVBO);
}

unsigned int Engine::GenerateACubemap(int width, int height)
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
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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
    hdrTexture = LoadHDRTexture("src/textures/HWSign3-Fence_2k.hdr");

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

void Engine::ShutDown()
{
    delete &basicShader;
}

//version 4.5
unsigned int Engine::LoadTexture(const char *path)
{
    GLuint texture;

    int width, height, channel;
    unsigned char *data = stbi_load(path, &width, &height, &channel, 0);

    GLenum internalFormat = (channel == 4) ? GL_RGBA8 : GL_RGB8;
    GLenum dataFormat = (channel == 4) ? GL_RGBA : GL_RGB;

    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    //GL_INVALID_VALUE is generated if width, height or levels are less than 1.
    glTextureStorage2D(texture, 1, internalFormat, width, height);
   
    glTextureSubImage2D(texture, 0, 0, 0, width, height, dataFormat, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return texture;
}

unsigned int Engine::LoadHDRTexture(char const* path)
{
    stbi_set_flip_vertically_on_load(true);

    unsigned int textureId;
    int width, height, nrComponents;
    float *data = stbi_loadf(path, &width, &height, &nrComponents, 0);
    if (data)
    {
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Failed to load HDR image." << std::endl;
    }

    return textureId;
}

unsigned int Engine::LoadTextureOld(char const * path)
{
    unsigned int textureID;

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);

    assert(data);
   
    GLenum format;
    if (nrComponents == 1)
        format = GL_RED;
    else if (nrComponents == 3)
        format = GL_RGB;
    else if (nrComponents == 4)
        format = GL_RGBA;

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return textureID;
}


