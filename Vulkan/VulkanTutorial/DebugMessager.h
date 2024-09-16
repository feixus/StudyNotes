#pragma once

#include <vulkan/vulkan.h>

#include "ConstDefine.h"


class DebugMessager {
public:
	DebugMessager(const VkInstance& instance);
	~DebugMessager();

	static VkDebugUtilsMessengerCreateInfoEXT CreateDebugMessengerCreateInfo();

	void CreateDebugUtilsMessager(const VkInstance& instance);

private:
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessager;
};