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

    // Do this for mac compatability.
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

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

    // Binds the 'framebuffer_size_callback' method to the window resize event.
    glfwSetFramebufferSizeCallback(window, WindowResize);

    this->SetupOpenGlRendering();

    // Start game loop.
    while (!glfwWindowShouldClose(this->window))
    {
        // Calculate the elapsed time between the current and previous frame.
        float m_frameTime = (float)glfwGetTime();
        float m_deltaTime = m_frameTime - this->lastFrameTime;
        this->lastFrameTime = m_frameTime;

        glfwPollEvents();
        this->ProcessInput(this->window);

        glClearColor(this->clearColor.x, this->clearColor.y, this->clearColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Application logic
        this->Update(m_deltaTime);
        this->Draw();

        glfwSwapBuffers(this->window);
    }

    glfwTerminate();

    return 1;
}

void WindowResize(GLFWwindow *a_window, int a_width, int a_height)
{
    glViewport(0, 0, a_width, a_height);

    // TODO: Do your resize logic here...
}

void Engine::ProcessInput(GLFWwindow *a_window)
{
    // TODO: Process your input here...

    // If the escape key gets pressed, close the window.
    if (glfwGetKey(a_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(a_window, true);
}

void Engine::SetupOpenGlRendering()
{
    // TODO: Setup OpenGL code here...
    basicShader = new Shader("src/shaders/basicVertexShader.glsl", "src/shaders/basicFragmentShader.glsl");
    
    CreateACube();
    
    std::cout << glGetError() << "  -->>>>" << std::endl;

    glPointSize(50);
}

void Engine::Update(float a_deltaTime)
{
    // TODO: Update your logic here...
}

void Engine::Draw()
{
    float f = (float)glfwGetTime() * (float)M_PI * 0.1f;
    glm::mat4 mv_matrix = glm::mat4(1.0f);
    glm::translate(mv_matrix, glm::vec3(0.0f, 0.0f, -4.0f));
    glm::translate(mv_matrix, glm::vec3(sinf(2.1f * f) * 0.5f, cosf(1.7f * f) * 0.5f, sinf(1.3f * f) * cosf(1.5f * f) * 2.0f));
    glm::rotate(mv_matrix, glm::radians((float)glfwGetTime() * 45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::rotate(mv_matrix, glm::radians((float)glfwGetTime() * 81.0f), glm::vec3(1.0f, 0.0f, 0.0f));
       
    glm::mat4 proj_matrix = glm::mat4(1.0f);
    glm::perspective(glm::radians(50.0f), (float)this->screenWidth / this->screenHeight, 0.1f, 1000.0f);

    static const glm::vec4 bgColor(0.2f, 0.4f, 0.5f, 1.0f);
    glClearBufferfv(GL_COLOR, 0, &bgColor[0]);

    basicShader->use();
    basicShader->setMat4("mv_matrix", mv_matrix);
    basicShader->setMat4("proj_matrix", proj_matrix);

    glDrawArrays(GL_TRIANGLES, 0, 18);
}

void Engine::ShutDown()
{
    delete &basicShader;
}

void Engine::CreateACube()
{
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    static const GLfloat vertex_positions[] = 
    {
        -0.25f, 0.25f, -0.25f,
        -0.25f, -0.25f, -0.25f,
        0.25f, -0.25f, -0.25f,

        0.25f, -0.25f, -0.25f,
        0.25f, 0.25f, -0.25f,
        -0.25f, 0.25f, -0.25f,

        0.25, 0.25, 0.25,
        0.25, -0.25, 0.25,
        -0.25, 0.25, 0.25,

        0.25, -0.25, -0.25,
        -0.25, -0.25, 0.25,
        -0.25, 0.25, 0.25,



        -0.25f, 0.25f, -0.25f,
        0.25f, 0.25f, -0.25f,
        0.25f, 0.25f, 0.25f,

        0.25f, 0.25f, 0.25f,
        -0.25f, 0.25f, 0.25f,
        -0.25f, 0.25f, -0.25f

    };

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);


}