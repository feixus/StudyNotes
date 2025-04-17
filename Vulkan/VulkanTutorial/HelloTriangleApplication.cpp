#include "MacrosDefine.h"
#include "HelloTriangleApplication.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <optional>
#include <set>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <fstream>
#include <array>
#include <chrono>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include "ConstDefine.h"
#include "ConstStruct.h"
#include "HelpUtility.h"


static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
	app->framebufferResized = true;
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
	app->fov += static_cast<float>(-yoffset * 1.f);
}

bool checkValidationLayerSupport() {
	uint32_t layerOut;
	vkEnumerateInstanceLayerProperties(&layerOut, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerOut);
	vkEnumerateInstanceLayerProperties(&layerOut, availableLayers.data());

	for (const char* layerName : validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}

	return true;
}

std::vector<const char*> getRequiredExtensions() {
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

	if (enableValidationLayers) {
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	//for mac
	extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

	return extensions;
}

QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
			if (presentSupport) {
				indices.presentFamily = i;
			}
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	return indices;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto& extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}

SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
	SwapChainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}

bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
	QueueFamilyIndices indices = findQueueFamilies(device, surface);
	
	bool extensionsSupported = checkDeviceExtensionSupport(device);
	
	bool swapChainAdequate = false;
	if (extensionsSupported) {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}
	
	VkPhysicalDeviceFeatures supportedFeatures;
	vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
	
	return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice physicalDevice) {
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	for (const auto& availableFormat : availableFormats) {
		if (VK_FORMAT_B8G8R8A8_SRGB == availableFormat.format &&
			VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == availableFormat.colorSpace) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
	for (const auto& availablePresentMode : availablePresentModes) {
		if (VK_PRESENT_MODE_MAILBOX_KHR == availablePresentMode) {
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

//pixels and screen coordinates
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window ) {
	if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
		return capabilities.currentExtent;
	}
	else {
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);

		VkExtent2D actualExtent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)
		};

		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return actualExtent;
	}
}

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice,  const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (VkFormat format : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supportd format!");
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
	return findSupportedFormat(
		physicalDevice,
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module!");
	}

	return shaderModule;
}

//memoryType
//memoryHeaps: memory resources like dedicated VRAM and swap space in RAM for when VRAM runs out.
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags propertices) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if (typeFilter & (1 << i) &&
			(memProperties.memoryTypes[i].propertyFlags & propertices) == propertices) {
			return i;
		}
	}

	throw std::runtime_error("failed to find suitable memory type!");
}

void createImage(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tilling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	//what kind of coordinate system the texels in the image are going to be addressed.
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	//VK_IMAGE_TILING_LINEAR: texels are laid out in row-major order like our pixels array.
	//VK_IMAGE_TILING_OPTIMAL: texels are laid out in an implemetation defined order for optimal access.
	//tiling mode cant be changed at a later time. if want to directly access texels in the memory of the image, then must use VK_IMAGE_TILING_LINEAR. but we using a staging buffer instead of a staging imagee, so just using VK_IMAGE_TILING_OPTIMAL for efficient access from the shader.
	imageInfo.tiling = tilling;
	//VK_IMAGE_LAYOUT_UNDEFINED: not usable by the GPU and the very first transition will dicard the texels.
	//VK_IMAGE_LAYOUT_PREINITIALIZED: not usable by the GPU, but the first transition will preserve the texels.
	// if use an image as a staging image in combination with the VK_IMAGE_TILING_LINEAR layout, then upload the texel data to it and transition the image to be a transfer source without losing the data.
	// in this case, fist to transition the image to be a transfer destination and then copy texel data to it from a buffer object.
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// the image to be used as destination for the buffer copy, and to be able to access the image from the shader.
	imageInfo.usage = usage;
	//the image will only be used by one queue family: the one that supports graphics and transfer operations.
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	//related to multisampling. only relevant for images that will be used as attachments, so stick to one sample.
	imageInfo.samples = numSamples;
	//some optional flags for images that related to sparse images. where only certain regions are actually backed by memory. 3D texture for a voxel terrain for example.
	imageInfo.flags = 0;

	if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
		throw std::runtime_error("failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate image memory!");
	}

	vkBindImageMemory(device, image, imageMemory, 0);
}

VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void endSingleTimeCommands(VkDevice device, VkQueue graphicQueue,  VkCommandPool commandPool, VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(graphicQueue, 1, &submitInfo, VK_NULL_HANDLE);

	//can use a fence and wait with vkWaitForFences
	// or simply wait for the transfer queue to become idle with vkQueueWaitIdle
	// a fence can schedule multiple transfers simultaneously and wait for all of them complete, instead of executing one at a time.
	vkQueueWaitIdle(graphicQueue);

	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

bool hasStencilComponent(VkFormat format) {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

//vkCmdCopyBufferToImage requires the image to be in the right layout first. 
//perform layout transitions can using an image memory barrier.
//a pipeline barrier can used to synchronize access to resources, transition image layouts and transfer queue family ownership when VK_SHADING_MODE_EXCLUSIVE.
void transitionImageLayout(VkDevice device, VkQueue graphicQueue, VkCommandPool commandPool,  VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (hasStencilComponent(format)) {
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	//barriers are primarily used for synchronization purposes, so must specify which types of operations that involve the resource must happen before the barriers, and which operations that involve the resource must wait on the barrier.

	//access mask and pipeline stage need to set based on the layouts in the transition.
	// two transitions we need to handle:
	//	Undefined -> transfer destination: transfer writes that dont need to wait on anything.
	//	transfer destination -> shader reading: shader reads should wait on transfer writes, specifically the shader reads in the fragment shader,where to use the texture.
	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		// the depth buffer will be read from to perform depth tests to see if a fragment is visible, and will be written to when a new fragment is drawn.
		//the reading happens in the VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT stage and the writing in the VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 
		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	}
	else {
		throw std::invalid_argument("unsupportd layout transition!");
	}

	//srcStageMask: which pipeline stage the operations occur that should happen before the barrier.
	//dstStageMask: the pipeline stage in which operations will wait on the barrier.
	//dependencyFlags: VK_DEPENDENCY_BY_REGION_BIT --- turns the barrier into a per-region condition.
	// the last three pairs of parameters reference arrays of pipeline barriers of the three available types: 
	//	memory barriers, buffer memory barriers, image memory barriers.
	vkCmdPipelineBarrier(
		commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	endSingleTimeCommands(device, graphicQueue, commandPool, commandBuffer);
}

void createBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags propertices, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	//buffers can owned by a specific queue family or be shared between multiple at the same time.
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	//flags used to configue sparse buffer memory
	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create vertex buffer!");
	}

	//allocating memory for the buffer: query memory requirements.
	//size: the size of the required amount of memory in bytes
	//alignment: the offset in bytes where the buffer begins in the allocated region of memory, depends on bufferInfo.usage and bufferInfo.flags.
	//memoryTypeBits: Bit field of the memory types that are suitable for the buffer.
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, propertices);

	// the maximum number of simultaneous memory allocations is limited by the maxMemoryAllocationCount physical device limit. (4096 in RTX 4060
	//allocate memory for a large number of objects at the same time is to create a custom allocator that splits up a single allocation among 
	//	many different objects by using the offset parameters. such as VulkanMemoryAllocator library.
	if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate vertex buffer memory!");
	}

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void copyBufferToImage(VkDevice device, VkQueue graphicQueue, VkCommandPool commandPool, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = {
		width,
		height,
		1
	};

	vkCmdCopyBufferToImage(
		commandBuffer,
		buffer,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	endSingleTimeCommands(device, graphicQueue, commandPool, commandBuffer);
}

void generateMipmaps(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicQueue, VkCommandPool commandPool, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {

	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		throw std::runtime_error("texture image format does not support linear blitting!");
	}

	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	for (uint32_t i = 1; i < mipLevels; i++) {
		//transition level i-1 to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this transition will wait for level i-1 to be filled, 
		// either from the previous blit command , or from vkCmdCopyBufferToImage. the current blit command will wait on the transition.
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VkImageBlit blit{};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(commandBuffer,
			image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		//this barrier transitions mip level i-1 to VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL. this transition waits on the current blit command to finish.
		// all sampling operations will wait on the transition to finish.
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	//this barrier transitions the last mip level from transfer_dst to shader_read_only. this wasn't handled by the loop, since the last mip level is never blitted from.
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &barrier);

	endSingleTimeCommands(device, graphicQueue, commandPool, commandBuffer);
}

//memory transfer operation are executed using command buffers, just like drawing commands.
//allocate a temporary command buffer. create a separate command pool for short-lived buffers, so the implementation may be able to apply memory allocation optimizations.
void copyBuffer(VkDevice device, VkQueue queue, VkCommandPool commandPool, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
	VkCommandBuffer commandBuffer = beginSingleTimeCommands(device, commandPool);

	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	endSingleTimeCommands(device, queue, commandPool, commandBuffer);
}

/*-------local function--------------------------------------------------------------------------------------------------------------------- -*/


void HelloTriangleApplication::run() {
	initWindow();
	initVulkan();
	mainLoop();
	cleanup();
}

void HelloTriangleApplication::initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Triangle", nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	glfwSetScrollCallback(window, scroll_callback);

}

void HelloTriangleApplication::initVulkan()
{
	createInstance();
	setupDebugMessager();
	createSurface();
	pickPhysicalDevice();
	createLogicDevice();
	createSwapChain();
	createImageViews();
	createRenderPass();
	createDescriptorSetLayout();
	createGraphicsPipeline();
	createCommandPool();
	createColorResources();
	createDepthResources();
	createFramebuffers();
	createTextureImage(TEXTURE_PATH.c_str());
	createTextureImageView();
	createTextureSampler();
	HelpUtility::loadModel(MODEL_PATH.c_str(), vertices, indices);
	createVertexBuffer();
	createIndexBuffer();
	createUniformBuffers();
	createDescriptorPool();
	createDescriptorSets();
	createCommandBuffers();
	createSyncObjects();

	HelpUtility::OutputDetailInfos(instance, physicalDevice, device, surface);
}

void HelloTriangleApplication::mainLoop() {
	while (!glfwWindowShouldClose(window)) {
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
			modelMatrix = glm::translate(modelMatrix, -leftDir * 0.001f);
		}
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
			modelMatrix = glm::translate(modelMatrix, leftDir * 0.001f);
		}
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
			modelMatrix = glm::translate(modelMatrix, -viewDir * 0.001f);
		}
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
			modelMatrix = glm::translate(modelMatrix, viewDir * 0.001f);
		}
		
		glfwPollEvents();
		
		drawFrame();
	}
		
	vkDeviceWaitIdle(device);
}

//1.1 render a frame in vulkan:
//wait for previous frame to finish
//acquire an image from the swap chain
//record a command buffer which draws the scene onto that image
//submit the recorded command buffer
//present the swap chain image
//1.2 synchronization :
//semaphores: add order between queue operations(submit), binary semaphore and timeline semaphore, the waiting only happens on the GPU
//fences: (host) ordering the execution on the CPU, if the host need to know when the GPU has finished something, use a fence
//semaphores used to specify the execution order of operation on the GPU while fences used to keep the CPU and GPU in sync with each other
//swapchain operation(semaphores) and wait for previous frame to finish(fences) need to apply sync
//wait for the previous frame to finish
void HelloTriangleApplication::drawFrame() {
	vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
	
	//acquire image from swapchain
	uint32_t imageIndex;
	//to be signaled when the presentation engine is finished using the image
	VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
	
	// VK_ERROR_OUT_OF_DATE_KHR: the swap chain has become incompatible with the surface and can no longer be used for rendering, eg. happens after window resize.
	//VK_SUBOPTIMAL_KHR: the swap chain can still be used to successfully present to the surface, but the surface properties are no longer matched exactly.
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapChain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error("failed to acquire swap chain image!");
	}
	
	vkResetFences(device, 1, &inFlightFences[currentFrame]);
	
	updateUniformBuffer(currentFrame);
	
	//record command buffer
	vkResetCommandBuffer(commandBuffers[currentFrame], 0);
	recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
	
	//submit the command buffer
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
	//the stage of graphic pipeline that writes to the color attachment need to wait
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
	
	//specify which semaphores to signal once the command buffers have finished execution
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;
	
	//the fence will be signaled when the command buffer finish execution, then you can safe to reuse the command buffer
	if (vkQueueSubmit(graphicQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
		throw std::runtime_error("failed to submit draw command buffer!");
	}
	
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	
	VkSwapchainKHR swapChains[] = { swapChain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	
	presentInfo.pResults = nullptr;
	result = vkQueuePresentKHR(presentQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
		framebufferResized = false;
		recreateSwapChain();
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("failed to acquire a swap chain image!");
	}
	
	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FFLIGHT;
}

void HelloTriangleApplication::recreateSwapChain() {
	int width = 0, height = 0;
	glfwGetWindowSize(window, &width, &height);
	while (width == 0 && height == 0) {
		glfwGetWindowSize(window, &width, &height);
		glfwWaitEvents();
	}
	vkDeviceWaitIdle(device);

	cleanupSwapChain();

	createSwapChain();
	createImageViews();
	createColorResources();
	createDepthResources();
	createFramebuffers();
}

void HelloTriangleApplication::updateUniformBuffer(uint32_t currentImage) {
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	glm::vec3 eye = glm::vec3(2.0f, 2.0f, 2.0f);
	glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);

	UniformBufferObject ubo{};
	ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.view = glm::lookAt(eye, center, up);

	viewDir = center - eye;
	glm::normalize(viewDir);

	leftDir = glm::cross(viewDir, up);

	ubo.proj = glm::perspective(glm::radians(fov), swapchainExtent.width / (float)swapchainExtent.height, 0.1f, 30.0f);
	//GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted.
	ubo.proj[1][1] *= -1;

	//copy the data in the uniform buffer object to the current uniform buffer. map the uniform buffer once.
	//pass a small buffer of data to shaders are push constants.
	memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
}

void HelloTriangleApplication::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	//one-time-submit / render-pass-continue(secondary command buffer) / simultaneous-use(resubmitted)
	beginInfo.flags = 0;
	//relevant for secondary command buffers
	beginInfo.pInheritanceInfo = nullptr;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("failed to begin command buffer!");
	}

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = swapChainFrameBuffers[imageIndex];

	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = swapchainExtent;

	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
	clearValues[1].depthStencil = { 1.0f, 0 };

	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	//subpass-contents-inline: the render pass command will be embedded in the primary command buffer itself
	//subpass-contents-secondary-command-buffers: the render pass commands will be executed from secondary command buffers
	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	//pipelineBindPoint : specify the pipeline object is graphics or compute pipeline or ray tracing...
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

	VkBuffer vertexBuffers[] = { vertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

	vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	//descriptor set for uniform buffer.
	//descriptor sets are not unique to graphics pipelines, therefore we need to specify that bind descriptor sets to the graphics or compute pipeline.
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

	//dynamic states
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(swapchainExtent.width);
	viewport.height = static_cast<float>(swapchainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = swapchainExtent;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	/*vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);*/
	vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to end command buffer!");
	}
}

void HelloTriangleApplication::cleanup() {
	cleanupSwapChain();

	vkDestroyPipeline(device, graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);

	for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
		vkDestroyBuffer(device, uniformBuffers[i], nullptr);
		vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
	}

	vkDestroyDescriptorPool(device, descriptorPool, nullptr);

	vkDestroySampler(device, textureSampler, nullptr);
	vkDestroyImageView(device, textureImageView, nullptr);

	vkDestroyImage(device, textureImage, nullptr);
	vkFreeMemory(device, textureImageMemory, nullptr);

	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

	vkDestroyBuffer(device, indexBuffer, nullptr);
	vkFreeMemory(device, indexBufferMemory, nullptr);

	vkDestroyBuffer(device, vertexBuffer, nullptr);
	vkFreeMemory(device, vertexBufferMemory, nullptr);

	for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; ++i) {
		vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
		vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
		vkDestroyFence(device, inFlightFences[i], nullptr);
	}

	vkDestroyCommandPool(device, commandPool, nullptr);

	if (enableValidationLayers) {
		debugMessager.reset();
	}

	vkDestroyDevice(device, nullptr);

	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);

	glfwTerminate();
}

void HelloTriangleApplication::cleanupSwapChain() {
	for (size_t i = 0; i < swapChainFrameBuffers.size(); ++i) {
		vkDestroyFramebuffer(device, swapChainFrameBuffers[i], nullptr);
	}

	for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
		vkDestroyImageView(device, swapChainImageViews[i], nullptr);
	}

	vkDestroyImageView(device, depthImageView, nullptr);
	vkDestroyImage(device, depthImage, nullptr);
	vkFreeMemory(device, depthImageMemory, nullptr);

	vkDestroyImageView(device, colorImageView, nullptr);
	vkDestroyImage(device, colorImage, nullptr);
	vkFreeMemory(device, colorImageMemory, nullptr);

	vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void HelloTriangleApplication::createInstance() {
	if (enableValidationLayers && !checkValidationLayerSupport()) {
		throw std::runtime_error("validaton layer requested, but not available!");
	}
		
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Hello Triangle";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;
		
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
		
	auto extensions = getRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();
		
	if (enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
		
		VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo = DebugMessager::CreateDebugMessengerCreateInfo();
		createInfo.pNext = &debugMessengerInfo;
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}
		
	createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		
	if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
		throw std::runtime_error("failed to create instance!");
	}
}

void HelloTriangleApplication::setupDebugMessager() {
	if (!enableValidationLayers) return;
	
	debugMessager = std::make_unique<DebugMessager>(instance);
}

void HelloTriangleApplication::createSurface() {
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window surface");
	}
}

void HelloTriangleApplication::pickPhysicalDevice() {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		throw std::runtime_error("failed to find GPU with Vulkan support");
	}
	
	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
	
	for (const auto& device : devices) {
		if (isDeviceSuitable(device, surface)) {
			physicalDevice = device;
			msaaSamples = getMaxUsableSampleCount(physicalDevice);
			break;
		}
	}
	
	if (physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void HelloTriangleApplication::createLogicDevice() {
	QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.emplace_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

	createInfo.pEnabledFeatures = &deviceFeatures;

	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else {
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
		throw std::runtime_error("failed to create logical device!");
	}

	vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicQueue);
	vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void HelloTriangleApplication::createSwapChain() {
	SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice, surface);

	VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

	//simply sticking to this minimum means that we may sometimes have to wait on the driver to complete internal operations before we can acquire another image to render to. therefore it is recommended to request at least one more image than the minimum.
	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
	uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
		throw std::runtime_error("failed to create swap chain!");
	}

	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
	swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

	swapChainImageFormat = surfaceFormat.format;
	swapchainExtent = extent;
}

void HelloTriangleApplication::createImageViews() {
	swapChainImageViews.resize(swapChainImages.size());
	for (size_t i = 0; i < swapChainImages.size(); i++) {
		swapChainImageViews[i] = HelpUtility::createImageView(device, swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}

void HelloTriangleApplication::createRenderPass() {
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = msaaSamples;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	//image_layout_color_attachment: image used as color attachment
	// image_layout_present_src: image to be presented in the swap chain
	// image_layout_transfer_dst: image to be used as destination for a memory copy operation

	//which layout the image will have before the render pass begins
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//the layout to automatically transition to when the render pass finish -> VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	//!!! but multisampled images cant be present directly. firt need to resolve then to a regular image.
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


	//render pass -> subpasses -> references attachments -> attachments(color buffer or depth buffer...)
	VkAttachmentReference colorAttachmentRef{};
	//the index of the attachment in the array id directly reference from the fragment shader with the layout(location=0) out vec4 outColor
	colorAttachmentRef.attachment = 0;
	//vulkan will automatically transition the attachment to this layout
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = findDepthFormat(physicalDevice);
	depthAttachment.samples = msaaSamples;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef{};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription colorAttachmentResolve{};
	colorAttachmentResolve.format = swapChainImageFormat;
	colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentResolveRef{};
	colorAttachmentResolveRef.attachment = 2;
	colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	//graphic subpass
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	//input attachments / resolve attachment / depthandstencil attachment / preserve attachment
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;
	subpass.pResolveAttachments = &colorAttachmentResolveRef;

	std::array<VkAttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	//https://registry.khronos.org/vulkan/specs/1.0/html/chap8.html#renderpass-creation
	//srcSubpass is the subpass index of the first subpass in the dependency or VK_SUBPASS_EXTERNAL
	//dstSubpass is the subpass index of the second subpass in the dependency or VK_SUBPASS_EXTERNAL
	//srcStageMask/dstStageMask  -> VkPipelineStageFlagBits
	//srcAccessMask/dstAccessMask  ->  VkAccessFlagBits
	VkSubpassDependency dependency{};
	//indices of the dependency | the dependent subpass
	//VK_SUBPASS_EXTERNAL refers to the implicit subpass before or after the render pass
	//dstSubpass must always be higher than srcSubpass to prevent cycles in the dependency graph, except VK_SUBPASS_EXTERNAL
	//if srcSubpass is equal to VK_SUBPASS_EXTERNAL, the first synchronization scope includes commands that occur earlier in submission order than the vkCmdBeginRenderPass used to begin the render pass instance.
	//	otherwise, the first set of commands includes all commands submitted as part of the subpass instance identified by srcSubpass and any load,store or multisample resolve operations on attachments used in srcSubpass.
	//	the first synchronization scope is limited to operations on the pipeline stages determined by the source stage mask.
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;

	//specify the operations to wait on and the stages in which these operations occur.
	//wait for the swap chain to finish reading from the image before we can access it
	// there is no conflict between the transitioning of the depth image and it being cleared as part of its load operation
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;

	//prevent the transition from happening until it's actually necessary: when we want to start writing colors to it.
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
		throw std::runtime_error("failed to create render pass!");
	}
}

void HelloTriangleApplication::createDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	//specify in which shader stages the descriptor is going to be referenced.
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	//for image sampling related descriptors.
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	samplerLayoutBinding.pImmutableSamplers = nullptr;

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void HelloTriangleApplication::createGraphicsPipeline() {
	//1.shader info
	auto vertShaderCode = HelpUtility::readFile("shaders/vert.spv");
	auto fragShaderCode = HelpUtility::readFile("shaders/frag.spv");

	VkShaderModule vertShaderModule = createShaderModule(device, vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(device, fragShaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	//2.fixed functions

	//2.1 dynamic state: changes without recreating the pipeline state at draw time
	//eg. the size of viewport / line width / blend constants / depth test / front face ......
	std::vector<VkDynamicState> dynamicStates = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	//2.2 vertex input : 
	// bindings -> spacing between data / per-vertex or per-instance
	// attribute descriptions : binding to load them from and at which offset
	//for vertex buffer to do
	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	//2.3 input assembly:
	//		which kind of geometry will be drawn from the vertices
	//		if primitive restart should be enabled -> possible to break up lines and triangles in the _strip topology modes by index of 0xFFFF or 0xFFFFFFFF
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	//2.4 viewport and scissors:
	//		viewport is the region of the framebuffer, generally (0,0) to (width, height)
	//the size of swap chain and its images may differ from the width and height of the window
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = (float)swapchainExtent.width;
	viewport.height = (float)swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	//viewport define the transform from the image to the framebuffer, scissor rectangles define in which region pixels will stored
	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = swapchainExtent;

	// viewports and scissor rectangles can either as the static part of pipeline or as dynamic state set in the command buffer

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;


	//2.5 rasterizer
	//		take the geometry and turn it into fragments
	//		perform depth testing / scissor test / face culling / wireframe rendering
	VkPipelineRasterizationStateCreateInfo rasterizerStateInfo{};
	rasterizerStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerStateInfo.depthClampEnable = VK_FALSE;
	rasterizerStateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;  //fill / line / point
	rasterizerStateInfo.lineWidth = 1.0f;
	rasterizerStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	//because of the Y-flip we did in the projection matrix, the vertices are now being drawn in counter-clockwise order.
	//this causes backface culling to kick in and prevents any geometry from being drawn.
	rasterizerStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizerStateInfo.depthBiasEnable = VK_FALSE;
	rasterizerStateInfo.depthBiasConstantFactor = 0.0f;
	rasterizerStateInfo.depthBiasClamp = 0.0f;
	rasterizerStateInfo.depthBiasSlopeFactor = 0.0f;

	//2.6 multisampling
	VkPipelineMultisampleStateCreateInfo multisamplingStateInfo{};
	multisamplingStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingStateInfo.sampleShadingEnable = VK_FALSE;
	multisamplingStateInfo.rasterizationSamples = msaaSamples;
	multisamplingStateInfo.minSampleShading = 1.0f;
	multisamplingStateInfo.pSampleMask = nullptr;
	multisamplingStateInfo.alphaToCoverageEnable = VK_FALSE;
	multisamplingStateInfo.alphaToOneEnable = VK_FALSE;

	//2.7 depth and stencil test
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	//keep fragments that fall within the specified depth range
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
	//stencil buffer operation
	depthStencil.stencilTestEnable = VK_FALSE;
	depthStencil.front = {};
	depthStencil.back = {};

	//2.8 color blending
	//		mix the old and new one to product a final color / combine the old and new one using a bitwise operation
	//		VkPipelineColorBlendAttachmentState(per attached framebuffer) and VkPipelineColorBlendStateCreateInfo to config color blending
	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	//alpha blending
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo{};
	colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateInfo.logicOpEnable = VK_FALSE;  //the switch for bitwise combination
	colorBlendStateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &colorBlendAttachment;
	colorBlendStateInfo.blendConstants[0] = 0.0f;
	colorBlendStateInfo.blendConstants[1] = 0.0f;
	colorBlendStateInfo.blendConstants[2] = 0.0f;
	colorBlendStateInfo.blendConstants[3] = 0.0f;

	//2.9 pipeline layout
	//		uniform value eg. transform matrix / texture samplers
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
	//another way of passing dynamic values to shaders
	pipelineLayoutInfo.pushConstantRangeCount = 0;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}


	//graphic pipeline: combine shader stages and fix-function and pipeline layout and render pass
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;

	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizerStateInfo;
	pipelineInfo.pMultisampleState = &multisamplingStateInfo;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicState;

	pipelineInfo.layout = pipelineLayout;

	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;

	//create a new graphic pipeline by deriving from an existing pipeline
	//can reuse and switch quicker, but need pipeline_create_derivative_bit
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	//pipeline cache can across program execution if the cache is stored to a file, can speed up pipeline creation
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphic pipeline!");
	}

	vkDestroyShaderModule(device, vertShaderModule, nullptr);
	vkDestroyShaderModule(device, fragShaderModule, nullptr);
	
}

void HelloTriangleApplication::createCommandPool() {
	QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, surface);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

	if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create command pool!");
	}
}

void HelloTriangleApplication::createColorResources() {
	VkFormat colorFormat = swapChainImageFormat;

	createImage(physicalDevice, device, 
		swapchainExtent.width, swapchainExtent.height, 1, msaaSamples, colorFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		colorImage, colorImageMemory);

	colorImageView = HelpUtility::createImageView(device, colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void HelloTriangleApplication::createDepthResources() {
	VkFormat depthFormat = findDepthFormat(physicalDevice);

	createImage(physicalDevice, device,
		swapchainExtent.width, swapchainExtent.height, 1, msaaSamples, depthFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		depthImage, depthImageMemory);

	depthImageView = HelpUtility::createImageView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	transitionImageLayout(device, graphicQueue, commandPool, depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
}

void HelloTriangleApplication::createFramebuffers() {
	//image from swapchain -> framebuffer(bind image to specific location in the render pass) -> rendering to the framebuffer -> present in swapchain
	swapChainFrameBuffers.resize(swapChainImageViews.size());
	for (size_t i = 0; i < swapChainFrameBuffers.size(); i++) {
		//the color attachment differs for every swap chain image, but the same depth image can be used by all of them
		//because only a single subpass is running at the same time dut to our semaphores.
		std::array<VkImageView, 3> attachments = {
			colorImageView,
			depthImageView,
			swapChainImageViews[i],
		};

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		framebufferInfo.pAttachments = attachments.data();
		framebufferInfo.width = swapchainExtent.width;
		framebufferInfo.height = swapchainExtent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFrameBuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create framebuffer!");
		}
	}
}

void HelloTriangleApplication::createTextureImage(const char* filename) {
	int texWidth, texHeight, texChannels;
	stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	//the pixels are laid out row by row with 4 bytes per pixel in the case of STBI_rgb_alpha
	VkDeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		throw std::runtime_error("failed to load texture image!");
	}

	mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	//create a buffer in host visible memory so that can use vkMapMemory and copy the pixels to it.
	createBuffer(physicalDevice, device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vkUnmapMemory(device, stagingBufferMemory);

	//clean up the original pixel array.
	stbi_image_free(pixels);

	// vkCmdBlitImage is considered a transfer operation, so we must inform Vulkan that we intend to use the texture image as both the source and destination of a transfer.
	createImage(physicalDevice, device,
		texWidth, texHeight,
		mipLevels,
		VK_SAMPLE_COUNT_1_BIT,
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		textureImage,
		textureImageMemory);

	//transition the texture image to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	transitionImageLayout(device, graphicQueue, commandPool, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

	//execute the buffer to image copy operation
	copyBufferToImage(device, graphicQueue, commandPool, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

	//to start sampling from the texture image in the shader, need one last transition to prepare it for shader access
	//  only performs layout transtions on the entire image, so need to write a few more pipeline barrier commands. so comment it.
	//transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);

	generateMipmaps(physicalDevice, device, graphicQueue, commandPool, textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevels);

}

void HelloTriangleApplication::createTextureImageView() {
	textureImageView = HelpUtility::createImageView(device, textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

//sampler dont reference a VkImage anywhere. the sampler id a distinct object that provides an interface to extract colors from a texture.
// it can be applied to any image you want, whether it is 1D,2D or 3D.
void HelloTriangleApplication::createTextureSampler() {
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(physicalDevice, &properties);
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	//mainly used for percentage-closer fitering on shadow maps.
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = static_cast<float>(mipLevels);

	if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
		throw std::runtime_error("failed to create texture sampler!");
	}
}

//buffers in Vulkan are regions of memory.
//buffers do not automatically allocate memory.
void HelloTriangleApplication::createVertexBuffer() {
	VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	//VK_BUFFER_USAGE_TRANSFER_SRC_BIT£º buffer can be used as source in a memory transfer operation
	//VK_BUFFER_USAGE_TRANSFER_DST_BIT: buffer can be used as destination in a memory transfer operation.
	createBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	//mapping the buffer memory into CPU accessible memory
	// access a region of the specified memory resource defined by an offset and size.
	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	//the driver may not immediately copy the data info the buffer memory, for example because of caching
	//also write to the buffer are not visible in the mapped memory yet.
	//so  use a memory heap that is host coherent, indicated with VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	// or call vkFlushMappedMemoryRanges after writing to the mapped memory, and call vkInvalidateMappedMemoryRanges before reading from the mapped memory.
	//we went for the first approach, which ensures that the mappde memory always matched the contents of the allocated memory. this may lead to slightly worse performance than explicit flushing.
	// flushing memory ranges or using a coherent memory heap means that the driver will be aware of our writes to the buffer, but not actually visible on the GPU yet. 
	// the transfer of data to the GPU is an operation that happens in the background and vkQueueSubmit call.
	memcpy(data, vertices.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	// the vertexBuffer now allocated from a memory type that is device local, which is in GPU side, cant use vkMapMemory.
	// so we can copy data from the staging buffer to the vertexBuffer.
	// the transfer source flag for the stagingBuffer and the transfer destination flag for the vertexBuffer, along with the vertex buffer usage flag.
	createBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vertexBuffer, vertexBufferMemory);

	copyBuffer(device, graphicQueue, commandPool, stagingBuffer, vertexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void HelloTriangleApplication::createIndexBuffer() {
	VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, indices.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	createBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		indexBuffer, indexBufferMemory);

	copyBuffer(device, graphicQueue, commandPool, stagingBuffer, indexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void HelloTriangleApplication::createUniformBuffers() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	uniformBuffers.resize(MAX_FRAMES_IN_FFLIGHT);
	uniformBuffersMemory.resize(MAX_FRAMES_IN_FFLIGHT);
	uniformBuffersMapped.resize(MAX_FRAMES_IN_FFLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
		createBuffer(physicalDevice, device, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			uniformBuffers[i], uniformBuffersMemory[i]);
		// persistent mapping
		vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
	}
}

void HelloTriangleApplication::createDescriptorPool() {
	std::array<VkDescriptorPoolSize, 2> poolSizes{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT);
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT);

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT);

	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor pool!");
	}
}

void HelloTriangleApplication::createDescriptorSets() {
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FFLIGHT, descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT);
	allocInfo.pSetLayouts = layouts.data();

	descriptorSets.resize(MAX_FRAMES_IN_FFLIGHT);
	if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate descriptor sets!");
	}

	//descriptors that refer to buffers, like uniform buffer description, are configured with a VkDescriptorBufferInfo struct.
	for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = uniformBuffers[i];
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = textureImageView;
		imageInfo.sampler = textureSampler;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		//the configuration of descriptors is updated using the vkUpdateDescriptorSets function, which takes an array of vkWriteDescriptorSet structs as parameter.
		std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSets[i];
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSets[i];
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}
}

void HelloTriangleApplication::createCommandBuffers() {
	commandBuffers.resize(MAX_FRAMES_IN_FFLIGHT);

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	//command buffers are primary or secondary command buffers
	// primary command buffers: can be submitted to a queue for execution, but cant be called from other command buffers
	// secondary command buffers: cant be submitted directly, but can be called from primary command buffers
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

	if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate command buffer!");
	}
}

void HelloTriangleApplication::createSyncObjects() {
	imageAvailableSemaphores.resize(MAX_FRAMES_IN_FFLIGHT);
	renderFinishedSemaphores.resize(MAX_FRAMES_IN_FFLIGHT);
	inFlightFences.resize(MAX_FRAMES_IN_FFLIGHT);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	// the first appear in signaled state
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; ++i) {
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
			vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create sync objects semaphore or fence!");
		}
	}
}


