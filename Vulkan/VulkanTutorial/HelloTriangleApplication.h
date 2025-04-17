#pragma once

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "Vertex.h"
#include "DebugMessager.h"
#include <vector>


class HelloTriangleApplication {

public:
	void run();

private:
	void initWindow();
	/*1. vulkan instance
	* 2. validation layer : instance layer   and message callback(VK_EXT_debug_utils)
	* 3. physical devices and queue families : compute/graphic/memory transfer queue families
	* 4. windows surface
	* 5. swap chain : surface format(color depth)/ present mode(FIFO|FIFO_RELAX|MAILBOX(triple buffering)|IMMEDIATE) / swap extent(resolution of images in swap chain)
	* 6. image views
	* 7. graphic pipeline: vertex/fragment shader(SPIR-V bytecode) / fixed-function configure
	* 8. render pass object: render pass -> subpass -> attachmentRef -> attachments, framebuffer attachment / color and depth buffer / samples
	*		subpass system let the driver optimize its resources. it reduces the amount of memory that needs to be allocated with deferred rendering.
	* 9. framebuffer object : act as a canvas
	* 10. command pool and command buffers: command buffers will be automatically freed when the command pool is destroyed, dont need explict destroy
	* 11. vertex buffer
	* 12. staging buffer: memory type used in CPU, such as host-visible/host-coherent, may not the most optimal memory type for the GPU. device local memory is the most optimal memory for GPU.
	*		and is not accessible by the CPU. so we need staging buffer as a bridge, send vertices in c++ to staging buffer with vkMapMemory, use buffer copy command to send vertexbuffer to device local memory.
	*		using staging buffer for transferring data from host to device memory unless a transfer must be performed every frame.
	*		mobile architectures generally have memory heaps that are both DEVICE_LOCAL and HOST_VISIBLE/COHERENT.
	* 13. index buffer: we should allocate multiple resources like buffers from a single memory allocation, but in fact you can store multiple buffers, like the vertex and index buffers,
	*		into a single VkBuffer and use offsets in commands like vkCmdBindVertexBuffers(Vulkan Memory Management). so the data is more cache friendly, because closer together.
	*		it is even possible to reuse the same chunk of memory for multiple resources if they are not used during the same render operations, known as aliasing.
	* 14. resource descriptors: for shaders to freely access resource like buffers and images.
	*		specify a descriptor layout during pipeline creation
	*		allocate a descriptor set from a descriptor pool
	*		bind the descriptor set during rendering
	*
	*		descriptor layout specifies the types of resources just like a render pass specifies the types of attachments
	*		a descriptor set specifies the actual buffer or image resources just like a framebuffer specifies the actual image views.
	*
	*	uniform buffer objects(UBO) is one of the descriptors. first create uniformBufferObject struct data in c++ side, then create uniform buffer and memory block,
	*		then vkMapMemory to connect data and uniform buffer. then create descriptor set by descriptor set layout and descriptor set pool,
	*		then config the descriptor set with VkDescriptorBufferInfo , which refer to uniform buffer. last update the descriptor set with vkUpdateDescriptorSets.
	* 15. image
	*		create an image object backed by device memory.
	*		fill it with pixels from an image file.
	*		create an image sampler and image view.
	*		add a combined image sampler descriptor to sample colors from the texture.
	*	vulkan can copy pixels from a VkBuffer to an image.
	*	image layout: VK_IMAGE_LAYOUT_PRESENT_SRC_KHR/VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL/VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL/VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL/VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	*	pipeline barrier to transition the layout of an image. primarily used for synchronizing access to resources. barriers can additionally be used to transfer queue family ownership when using VK_SHADING_NODE_EXCLUSIVE.
	*
	* combined image sampler: new type of descriptor, the descriptor makes it possible for shaders to access an image resource through a sampler object.
	* this also need descriptor layout, descriptor pool and descriptor set .
	*
	* submit commands so far have been set up to execute synchronously by waiting for the queue to become idle. but combine these operation in a single command buffer and execute asynchronously for higher throughput.
	*
	* 16. depth buffer
	*		sort all of the draw calls by depth from back to front for drawing transparent objects
	*		use depth testing with a depth buffer
	*		depth attachment is base on an image, just like color attachment. the difference is that the swap chain will not automatically create depth images for us.
	*	so we need image, memory and image view.
	*		unlike the texture image, dont need a specific format, because dont directly accessing the texels from the program. at least 24 bits is common
	*	VK_FORMAT_D32_SFLOAT/VK_FORMAT_D32_SFLOAT_S8_UINT/VK_FORMAT_D24_UNORM_S8_UINT
	*		after create depth image, we dont need to map depth image or copy another image to it, just clear it at the start of the render pass like the color attachment.
	*
	* first we need create depth image/depth device local memory/depth image view, then transition the layout of the image to depth attachment. then need add depth attachment in the renderpass create.
	* then we need bind the depth image to the depth attachment in framebuffer creation. last set the depthstencil state in pipeline state.
	*
	* 17. load model
	*	an obj file consists of positions, normals, texture coordinates and faces. faces consist of an arbitrary amount of vertices, where each vertex refers to a position,normal,texture coordinate by index.
	*	the attrib container -> positions,normals,texture coordinates
	*	the shapes container -> separate objects and their faces
	*	each face consists of an array of vertices, and each vertex contains the indices of the position,normal,texture coordinate attributes.
	*
	* 18. mipmaps
	*	each of the mip images is stored in diffrent mip levels of a VkImage. Mip level 0 is the original image, the mip levels after level 0 are commonly referred to as the mip chain.
	*	texture image has multiple mip levels, but the staging buffer can only be used to fill mip level 0. the other levels to fill we need to generate the data from the single level that we have.
	*	vkCmdBlitImage command can perform copying, scaling and filtering operations. call this multiple times to blit data to each level of our texture image.
	*like other image operations, vkCmdBlitImage depends on the layout of the image it operates on.
	*
	* 19. msaa
	*	in msaa, each pixel is sampled in an offscreen buffer which is then rendered to the screen. this new buffer store more than one sample per pixel.
	*	once a multisampled buffer is created, it has to be resolved to the default framebuffer(which stores only a single sample per pixel).
	*	so we need to create an additional render target, to modify in graphics pipeline/framebuffer/render pass.
	*	but msaa only smoothens out the edges of geometry but not the interior filling. so need Sample Shading to solve shader aliasing.
	*
	* to do:
		Push constants
		Instanced rendering
		Dynamic uniforms
		Separate images and sampler descriptors
		Pipeline cache
		Multi-threaded command buffer generation
		Multiple subpasses
	*/
	void initVulkan();
	void createInstance();
	void setupDebugMessager();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicDevice();
	void createSwapChain();
	void createImageViews();
	void createRenderPass();
	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createCommandPool();
	void createColorResources();
	void createDepthResources();
	void createFramebuffers();
	void createTextureImage(const char* filename);
	void createTextureImageView();
	void createTextureSampler();
	void createVertexBuffer();
	void createIndexBuffer();
	void createUniformBuffers();
	void createDescriptorPool();
	void createDescriptorSets();
	void createCommandBuffers();
	void createSyncObjects();

	void mainLoop();
	void drawFrame();
	void recreateSwapChain();
	void updateUniformBuffer(uint32_t currentImage);
	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

	void cleanup();
	void cleanupSwapChain();

public:
	float fov = 45.0f;
	bool framebufferResized = false;

private:
	GLFWwindow* window;

	glm::mat4 modelMatrix = glm::mat4(1.0f);
	glm::vec3 viewDir = glm::vec3();
	glm::vec3 leftDir = glm::vec3();

	uint32_t currentFrame = 0;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	//an image has been acquired from the swapchain and is ready for rendering, rendering has finished and presentation can happen
	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;

	VkInstance instance;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	VkDevice device;
	VkQueue graphicQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	std::vector<VkImage> swapChainImages;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkFramebuffer> swapChainFrameBuffers;
	VkFormat swapChainImageFormat;
	VkExtent2D swapchainExtent;

	VkRenderPass renderPass;
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	VkPipeline graphicsPipeline;

	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> commandBuffers;

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;

	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;

	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;

	uint32_t mipLevels;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;

	VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

	VkImage colorImage;
	VkDeviceMemory colorImageMemory;
	VkImageView colorImageView;

	std::unique_ptr<DebugMessager> debugMessager;
};


