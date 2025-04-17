#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "Vertex.h"

class HelpUtility {
public:
	static VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	static std::vector<char> readFile(const std::string& filename);
	static void loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
	static void OutputDetailInfos(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface);
};