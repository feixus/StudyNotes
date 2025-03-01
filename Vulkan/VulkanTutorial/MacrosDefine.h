#pragma once

//glfw
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_EXPLOSE_NATIVE_WIN32
#define GLFW_INCLUDE_VULKAN
//glm
#define GL_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
//the perspective projection matrix generated by GLM will use the OpenGL depth range of -1.0 to 1.0 by default
//use the Vulkan range of 0.0 to 1.0 need this definition
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
// stb image
#define STB_IMAGE_IMPLEMENTATION
// tiny obj
#define TINYOBJLOADER_IMPLEMENTATION
