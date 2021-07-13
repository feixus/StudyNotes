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
    glEnable(GL_DEPTH_TEST);

    // TODO: Setup OpenGL code here...
    basicShader = new Shader("src/shaders/basicVertexShader.glsl", "src/shaders/basicFragmentShader.glsl");

    CreateACube();

    std::cout << glGetError() << "  -->>>>" << std::endl;

    glPointSize(50);
    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

void Engine::Update(float a_deltaTime)
{
    // TODO: Update your logic here...
}

void Engine::Draw()
{
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 3.0f) + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 perspective = glm::perspective(glm::radians(50.0f), (float)this->screenWidth / (float)this->screenHeight, 0.1f, 1000.0f);

    static const glm::vec4 bgColor(0.2f, 0.4f, 0.5f, 1.0f);
    glClearBufferfv(GL_COLOR, 0, &bgColor[0]);

    // 每次重新绘制时都需要清空深度缓冲和模板缓冲，否则允许深度测试时看不到渲染目标，为什么？
    glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0.0f);

    basicShader->use();
    basicShader->setMat4("vp_matrix", perspective * view);

    for (int i = 0; i < 24; i++)
    {
        float f = (float)i + (float)glfwGetTime() * (float)M_PI * 0.1f;
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, -20.0f));
        model = glm::translate(model, glm::vec3(sinf(2.1f * f) * 8.0f, cosf(1.7f * f) * 8.0f, sinf(1.3f * f) * cosf(1.5f * f) * 8.0f));
        model = glm::rotate(model, glm::radians((float)glfwGetTime() * 45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians((float)glfwGetTime() * 81.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        basicShader->setMat4("m_matrix", model);

        glDrawArrays(GL_TRIANGLES, 0, 36);
    }
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
            -1.0f, 1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,

            1.0f, -1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,

            1.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f,

            -1.0f, -1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,

            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,

            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,

            -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,

            1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, 1.0f,

            -1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, 1.0f,

            1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, -1.0f

        };

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

    //为什么attribindex = 0只输入3个顶点,而shader里却定义的vec4,且能正常运行
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
}