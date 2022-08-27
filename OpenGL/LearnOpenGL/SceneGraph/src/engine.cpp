#include "engine.h"
#include "stb_image.h"
#include <memory>

Engine::Engine(int a_width, int a_height, const char* a_windowName)
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

    string path = "src/objs/planet/planet.obj";
    Model model(path);
    ourEntity = model;
    this->SetupOpenGlRendering();

    // Start game loop.
    while (!glfwWindowShouldClose(this->window))
    {
        // Calculate the elapsed time between the current and previous frame.
        float time = (float)glfwGetTime();
        this->deltaTime = time - this->lastFrameTime;
        this->lastFrameTime = time;

        this->ProcessInput(this->window);

        this->Draw();

        glfwSwapBuffers(this->window);
        glfwPollEvents();
    }

    glfwTerminate();

    return 1;
}


void Engine::SetupOpenGlRendering()
{
    stbi_set_flip_vertically_on_load(true);

    glEnable(GL_DEPTH_TEST);

    camera = new Camera(glm::vec3(0.0f, 0.0f, 3.0f));

    basicShader = new Shader("src/shaders/basic_vs.glsl", "src/shaders/basic_fs.glsl");

    ourEntity.transform.setLocalPosition( {10, 0, 0} );
    const float scale = 0.75;
    ourEntity.transform.setLocalScale({ scale, scale, scale });
    {
        Entity* lastEntity = &ourEntity;
        for (unsigned int i = 0; i < 10; ++i)
        {
            lastEntity->addChild(*ourEntity.pModel);
            lastEntity = lastEntity->children.back().get();

            lastEntity->transform.setLocalPosition({10, 0, 0});
            lastEntity->transform.setLocalScale({scale, scale, scale});
        }
    }

    ourEntity.updateSelfAndChild();
   
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    std::cout << std::endl << "error code = " << glGetError() << "  -->>>>" << std::endl;
    std::cout << "opengl version: " << glGetString ( GL_SHADING_LANGUAGE_VERSION ) << std::endl;
}

void Engine::Draw()
{
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    basicShader->use();
    
    glm::mat4 projection = glm::perspective(glm::radians(camera->Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
    glm::mat4 view = camera->GetViewMatrix();
    basicShader->setMat4("projection", projection);    
    basicShader->setMat4("view", view);

    Entity* lastEntity = &ourEntity;
    while (lastEntity->children.size())
    {
        basicShader->setMat4("model", lastEntity->transform.getModelMatrix());
        lastEntity->pModel->Draw(*basicShader);
        lastEntity = lastEntity->children.back().get();
    }

    ourEntity.transform.setLocalRotation({0.f, ourEntity.transform.getLocalRotation().y + 20 * deltaTime, 0.f});
    ourEntity.updateSelfAndChild();
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
        camera->ProcessKeyboard(FORWARD, this->deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera->ProcessKeyboard(BACKWARD, this->deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera->ProcessKeyboard(LEFT, this->deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera->ProcessKeyboard(RIGHT, this->deltaTime);
}

void Engine::ShutDown()
{
    delete &basicShader;
}

