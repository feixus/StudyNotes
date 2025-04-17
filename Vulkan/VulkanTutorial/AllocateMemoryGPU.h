#pragma once

#include <vulkan/vulkan.h>

struct AllocatedBuffer {
	AllocatedBuffer(const VkPhysicalDevice& physicalDevice, const VkDevice& device, const VkBufferCreateInfo& bufferInfo, const VkMemoryPropertyFlags& flags);
	~AllocatedBuffer();

};