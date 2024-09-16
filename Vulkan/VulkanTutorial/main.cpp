#include <iostream>
#include <stdexcept>

#include "HelloTriangleApplication.h"


//Alignment requirements
/*
how exactly the data in the c++ structure should match with the uniform definition in the shader.
vulkan expects the data in your structure to be aligned in memory in a specific way, for example:
scalars have to be aligned by N(=4 bytes given 32 bit floats)
a vec2 must be aligned by 2N(=8 bytes)
a vec3 or vec4 must be aligned by 4N(=16 bytes)
a nested structure must be aligned by the base alignment of its members rounded up to a multiple of 16.
a mat4 matrix must have the same alignment as a vec4.

*/

// referenced blog
//https://vulkanppp.wordpress.com/2017/06/05/week-2-textures-uniform-buffers-descriptor-sets/
//https://github.com/Sankyr/graphics/tree/main/VulkanTest


int main() {
	HelloTriangleApplication app;

	try {
		app.run();
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}