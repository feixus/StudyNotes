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

    stbi_set_flip_vertically_on_load(true);

    camera = new Camera(glm::vec3(0.0f, 0.0f, 23.0f));

    basicShader = new Shader("src/shaders/basic_vs.glsl", "src/shaders/basic_fs.glsl");

    basicShader->use();
    basicShader->setInt("albedoMap", 0);
    basicShader->setInt("normalMap", 1);
    basicShader->setInt("metallicMap", 2);
    basicShader->setInt("roughnessMap", 3);
    basicShader->setInt("aoMap", 4);

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
        // basicShader->setFloat("metallic", (float)row / (float)nrRows);
        for (int col = 0; col < nrColumns; ++col)
        {
            //clamp the roughness to 0.05-1.0 as perfectly smooth surface 
            //(roughness 0.0) tend to look a bit off on direct light
            // basicShader->setFloat("roughness", glm::clamp((float)col / (float)nrColumns, 0.05f, 1.0f));

            model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3((col - (nrColumns / 2)) * spacing, (row - (nrRows / 2)) * spacing, 0.0f));

            basicShader->setMat4("model", model);
            DrawSphere();
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
        DrawSphere();
    }
}

void Engine::ShutDown()
{
    delete &basicShader;
}

void Engine::DrawSphere()
{
    if (this->SphereVAO == 0)
    {
        glGenVertexArrays(1, &this->SphereVAO);

        glGenBuffers(1, &SphereVBO);
        glGenBuffers(1, &SphereEBO);

        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> uv;
        std::vector<glm::vec3> normals;
        std::vector<unsigned int> indices;

        const unsigned int X_SEGMENTS = 64;
        const unsigned int Y_SEGMENTS = 64;
        const float PI = 3.14159265359f;
        for (unsigned int x = 0; x <= X_SEGMENTS; x++)
        {
            for (unsigned int y = 0; y <= Y_SEGMENTS; y++)
            {
                float xSegment = (float)x / (float)X_SEGMENTS;
                float ySegment = (float)y / (float)Y_SEGMENTS;
                float phi = xSegment * 2.0f * PI;
                float theta = ySegment * PI;
                float xPos = cos(phi) * sin(theta);
                float yPos = cos(theta);
                float zPos = sin(phi) * sin(theta);

                positions.push_back(glm::vec3(xPos, yPos, zPos));
                uv.push_back(glm::vec2(xSegment, ySegment));
                normals.push_back(glm::vec3(xPos, yPos, zPos));
            }
        }

        bool oddRow = false;
        for (unsigned int y = 0; y < Y_SEGMENTS; y++)
        {
            if (!oddRow)  // even row: y == 0, y == 2; and so on
            {
                for (unsigned int x = 0; x <= X_SEGMENTS; x++)
                {
                    indices.push_back(y       * (X_SEGMENTS + 1) + x);
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                }
            }
            else 
            {
                for (int x = X_SEGMENTS; x >= 0; x--)
                {
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    indices.push_back(y       * (X_SEGMENTS + 1) + x);
                }
            }
            oddRow = !oddRow;
        }

        indexCount = static_cast<unsigned int>(indices.size());

        std::vector<float> data;
        for (unsigned int i = 0; i < positions.size(); i++)
        {
            data.push_back(positions[i].x);
            data.push_back(positions[i].y);
            data.push_back(positions[i].z);
            if (normals.size() > 0)
            {
                data.push_back(normals[i].x);
                data.push_back(normals[i].y);
                data.push_back(normals[i].z);
            }
            if (uv.size() > 0)
            {
                data.push_back(uv[i].x);
                data.push_back(uv[i].y);
            }
        }

        glBindVertexArray(SphereVAO);
        glBindBuffer(GL_ARRAY_BUFFER, SphereVBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, SphereEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        unsigned int stride = (3 + 3 + 2) * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    }

    glBindVertexArray(SphereVAO);
    glDrawElements(GL_TRIANGLE_STRIP, indexCount, GL_UNSIGNED_INT, 0);
}

void Engine::DrawCube()
{
    if (VAO == 0)
    {
        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);

        static const GLfloat vertex_positions[] =
            {
                -1.0f, 1.0f, -1.0f,  0.0f, 1.0f,
                -1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
                1.0f, -1.0f, -1.0f,  1.0f, 0.0f,

                1.0f, -1.0f, -1.0f,  0.0f, 0.0f,
                1.0f, 1.0f, -1.0f,   0.0f, 1.0f,
                -1.0f, 1.0f, -1.0f,  1.0f, 1.0f,

                1.0f, 1.0f, 1.0f,    0.0f, 1.0f,
                1.0f, -1.0f, 1.0f,   0.0f, 0.0f,
                -1.0f, -1.0f, 1.0f,  1.0f, 0.0f,

                -1.0f, -1.0f, 1.0f,  1.0f, 0.0f,
                -1.0f, 1.0f, 1.0f,   1.0f, 1.0f,
                1.0f, 1.0f, 1.0f,    0.0f, 1.0f,

                1.0f, 1.0f, 1.0f,    0.0f, 1.0f,
                1.0f, 1.0f, -1.0f,   0.0f, 0.0f,
                1.0f, -1.0f, -1.0f,  1.0f, 0.0f,

                1.0f, -1.0f, -1.0f,  1.0f, 0.0f,
                1.0f, -1.0f, 1.0f,   1.0f, 1.0f,
                1.0f, 1.0f, 1.0f,    0.0f, 1.0f,

                -1.0f, 1.0f, 1.0f,   0.0f, 1.0f,
                -1.0f, 1.0f, -1.0f,  0.0f, 0.0f,
                -1.0f, -1.0f, -1.0f, 1.0f, 0.0f,

                -1.0f, -1.0f, -1.0f, 1.0f, 0.0f,
                -1.0f, -1.0f, 1.0f,  1.0f, 1.0f,
                -1.0f, 1.0f, 1.0f,   0.0f, 1.0f,

                1.0f, -1.0f,  1.0f,  0.0f, 1.0f,
                1.0f, -1.0f, -1.0f,  0.0f, 0.0f,
                -1.0f, -1.0f, -1.0f,  1.0f, 0.0f,

                -1.0f, -1.0f, -1.0f, 1.0f, 0.0f,
                -1.0f, -1.0f, 1.0f,  1.0f, 1.0f,
                1.0f, -1.0f, 1.0f,   0.0f, 1.0f,

                -1.0f, 1.0f, -1.0f,  0.0f, 1.0f,
                1.0f, 1.0f, -1.0f,   0.0f, 0.0f,
                1.0f, 1.0f, 1.0f,    1.0f, 0.0f,

                1.0f, 1.0f, 1.0f,    1.0f, 0.0f,
                -1.0f, 1.0f, 1.0f,   1.0f, 1.0f,
                -1.0f, 1.0f, -1.0f,  0.0f, 1.0f,

            };

        glGenBuffers(1, &VBO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

        //为什么attribindex = 0只输入3个顶点,而shader里却定义的vec4,且能正常运行
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)(sizeof(GL_FLOAT) * 3));
        glEnableVertexAttribArray(1);
    }

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
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

void Engine::DrawQuad() 
{
    if (QuadVAO == 0)
    {
        static const GLfloat vertex_positions[] = {
            1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,

            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f, 1.0f
        };

        glGenVertexArrays(1, &QuadVAO);
        glBindVertexArray(QuadVAO);

        glGenBuffers(1, &QuadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, QuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)(sizeof(GL_FLOAT) * 3));
        glEnableVertexAttribArray(1);
    }

    glBindVertexArray(QuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

