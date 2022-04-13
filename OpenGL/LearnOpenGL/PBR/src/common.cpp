#include "common.h"

void Common::RenderSphere(unsigned int& sphereVAO, unsigned int& sphereVBO, unsigned int& sphereEBO, unsigned int& indexCount)
{
    if (sphereVAO != 0)
    {
        glBindVertexArray(sphereVAO);
        glDrawElements(GL_TRIANGLE_STRIP, indexCount, GL_UNSIGNED_INT, 0);
        return;
    }

    glGenVertexArrays(1, &sphereVAO);

    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

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

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), &data[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    unsigned int stride = (3 + 3 + 2) * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
}

void Common::RenderCube(unsigned int& cubeVAO, unsigned int& cubeVBO)
{
    if (cubeVAO == 0)
    {    
        glGenVertexArrays(1, &cubeVAO);
        glBindVertexArray(cubeVAO);

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

        glGenBuffers(1, &cubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

        //为什么attribindex = 0只输入3个顶点,而shader里却定义的vec4,且能正常运行
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)(sizeof(GL_FLOAT) * 3));
        glEnableVertexAttribArray(1);
    }

    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Common::RenderQuad(unsigned int& quadVAO, unsigned int& cubeVBO)
{
    if (quadVAO != 0)
    {
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        return;
    }

    unsigned int quadVBO;
    static const GLfloat vertex_positions[] = {
        1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,

        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glBindVertexArray(quadVAO);

    glGenBuffers(1, &quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_positions), vertex_positions, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GL_FLOAT) * 5, (void *)(sizeof(GL_FLOAT) * 3));
    glEnableVertexAttribArray(1);
}

unsigned int Common::LoadTexture(const char* path)
{

}

unsigned int Common::LoadTextureOld(const char* path)
{

}

unsigned int Common::LoadHDRTexture(char const* path)
{

}