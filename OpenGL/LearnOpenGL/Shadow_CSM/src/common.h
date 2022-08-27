#ifndef COMMON_H
#define COMMON_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <iostream>

class Common 
{
public:
    static void RenderSphere(unsigned int& sphereVAO, unsigned int& sphereVBO, unsigned int& sphereEBO, unsigned int& indexCount);
    static void RenderCube(unsigned int& cubeVAO, unsigned int& cubeVBO);
    static void RenderQuad(unsigned int& quadVAO, unsigned int& quadVBO);
    static void RenderQuadStrip(unsigned int& quadVAO, unsigned int& quadVBO);
    static void RenderCube_PNT(unsigned int& cubeVAO, unsigned int& cubeVBO);

    static unsigned int LoadTexture(const char* path);
    static unsigned int LoadTextureOld(const char* path);
    static unsigned int LoadHDRTexture(char const* path);
    

private:
    

};

#endif 