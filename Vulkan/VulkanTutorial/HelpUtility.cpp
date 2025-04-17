#include "MacrosDefine.h"
#include "HelpUtility.h"
#include <stdexcept>
#include <fstream>
#include <unordered_map>
#include <iostream>

#include <tiny_obj_loader.h>

#include "ConstStruct.h"


VkImageView HelpUtility::createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
    	throw std::runtime_error("failed to create texture image view!");
    }
    
    return imageView;
}

std::vector<char> HelpUtility::readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();
	return buffer;
}

void HelpUtility::loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
    	throw std::runtime_error(warn + err);
    }
    
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    
    for (const auto& shape : shapes) {
    	for (const auto& index : shape.mesh.indices)
    	{
    		Vertex vertex{};
    
    		//for simplicity, we will assume that every vertex is unique for now, hence the simple auto-increment indices.
    		vertex.pos = {
    			attrib.vertices[3 * index.vertex_index + 0],
    			attrib.vertices[3 * index.vertex_index + 1],
    			attrib.vertices[3 * index.vertex_index + 2],
    		};
    
    		//the OBJ format assumes a coordinate system where a vertical coordinate of 0 means the bottom of the image,
    		//however we uploaded our image into Vulkan in a top to bottom where 0 means the top of the image.
    		vertex.texCoord = {
    			attrib.texcoords[2 * index.texcoord_index + 0],
    			1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
    		};
    
    		vertex.color = { 1.0f, 1.0f, 1.0f };
    
    		//vertices 11484 -> 3566
    		if (uniqueVertices.count(vertex) == 0) {
    			uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
    			vertices.push_back(vertex);
    		}
    
    		indices.push_back(uniqueVertices[vertex]);
    	}
    }
    
}

void HelpUtility::OutputDetailInfos(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkSurfaceKHR surface) {
    std::cout << "\n================= available instance extensions properties =================================\n";

	uint32_t extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> extensionsAll(extensionCount);

	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensionsAll.data());

	for (const auto& extension : extensionsAll)
	{
		std::cout << '\t' << extension.extensionName << '\n';
	}
	std::cout << '\n';

	std::cout << "\n================= available instance layer properties =================================\n";
	uint32_t layerOut;
	vkEnumerateInstanceLayerProperties(&layerOut, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerOut);
	vkEnumerateInstanceLayerProperties(&layerOut, availableLayers.data());

	for (const auto& layerProperties : availableLayers) {
		std::cout << layerProperties.layerName << '\t' << layerProperties.description << '\n';
	}

	std::cout << "\n=================physical devices =================================\n";

	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

	for (const auto& device : devices) {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		std::cout << "\n==============device properties==================\n";
		std::cout << " device name= " << deviceProperties.deviceName << "  \n" <<
			" device type= " << deviceProperties.deviceType << "  \n" <<
			" device id= " << deviceProperties.deviceID << "  \n" <<
			" api version= " << deviceProperties.apiVersion << "  \n" <<
			" vendor id= " << deviceProperties.vendorID << "  \n" <<
			"  maxImageDimension2D= " << deviceProperties.limits.maxImageDimension2D << " \n" <<
			"  framebuffer color sample count= " << deviceProperties.limits.framebufferColorSampleCounts << " \n" <<
			" maxMemoryAllocationCount = " << deviceProperties.limits.maxMemoryAllocationCount << " \n" << std::endl;

		std::cout << "\n==============device features==================\n";
		std::cout << "\n geometryShader= " << deviceFeatures.geometryShader << "\n" <<
			" tessellationShader= " << deviceFeatures.tessellationShader << "\n" <<
			" multiViewport= " << deviceFeatures.multiViewport << "\n" <<
			" shaderFloat64= " << deviceFeatures.shaderFloat64 << "\n" <<
			" textureCompressionASTC_LDR= " << deviceFeatures.textureCompressionASTC_LDR << "\n" <<
			" textureCompressionETC2= " << deviceFeatures.textureCompressionETC2 << "\n" <<
			" textureCompressionBC= " << deviceFeatures.textureCompressionBC << "\n" <<
			" samplerAnisotropy= " << deviceFeatures.samplerAnisotropy << "\n" <<
			" occlusionQueryPrecise = " << deviceFeatures.occlusionQueryPrecise << std::endl;
	}


	std::cout << "\n=================Queue Family Properties=================================\n";

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

	std::cout << "\nQueue Families properties: \n";
	for (const auto& queueFamily : queueFamilies) {
		std::cout << " queueFlags= " << queueFamily.queueFlags << "\n" <<
			" queueCount= " << queueFamily.queueCount << "\n" <<
			" timestampValidBits= " << queueFamily.timestampValidBits << "\n" << std::endl;
	}

	std::cout << "\n------------------------------------------------------------\n";


	std::cout << "\n=================Swap Chain=================================\n";

	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);
	std::cout << "VkSurfaceCapabilitiesKHR: \n";
	std::cout << " minImageCount= " << details.capabilities.minImageCount << "\n" <<
		" maxImageCount= " << details.capabilities.maxImageCount << "\n" <<
		" currentExtent.width= " << details.capabilities.currentExtent.width << "\n" <<
		" currentExtent.height= " << details.capabilities.currentExtent.height << std::endl;

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
	details.formats.resize(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());
	std::cout << "Surface Format:\n";
	for (const auto& format : details.formats) {
		std::cout << " format= " << format.format << "   colorSpace= " << format.colorSpace << std::endl;
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
	details.presentModes.resize(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data());
	std::cout << "Surface Present Modes:\n";
	for (const auto& presentMode : details.presentModes) {
		std::cout << " presentMode= " << presentMode << std::endl;
	}

	std::cout << "\n------------------------------------------------------------\n";

}
