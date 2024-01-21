#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <optional>


const uint32_t WIDTH = 2048;
const uint32_t HEIGHT = 1024;

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
	const  bool enableValidationLayers = false;
#else
	const  bool enableValidationLayers = true;
#endif // NDEBUG


VkResult CreateDebugUtilsMessagerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}

	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessagerEXT(VkInstance instance,
	VkDebugUtilsMessengerEXT pDebugMessager, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, pDebugMessager, pAllocator);
	}
}


class HelloTriangleApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

private:
	GLFWwindow* window;

	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessager;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

	void initWindow() {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Triangle", nullptr, nullptr);
	}
	/*1. vulkan instance
	* 2. validation layer : instance layer   and message callback(VK_EXT_debug_utils)
	* 3. physical devices and queue families : compute/graphic/memory transfer queue families
	* */

	void initVulkan() {
		createInstance();
		setupDebugMessager();
		pickPhysicalDevice();
	}

	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}
	}

	void cleanup() {
		if (enableValidationLayers) {
			DestroyDebugUtilsMessagerEXT(instance, debugMessager, nullptr);
		}

		vkDestroyInstance(instance, nullptr);

		glfwDestroyWindow(window);

		glfwTerminate();
	}

	void createInstance() {
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

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (enableValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();

			populateDebugMessengerCreateInfo(debugCreateInfo);
			createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;

		} else {
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
		}

		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
			throw std::runtime_error("failed to create instance!");
		}

		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensionsAll(extensionCount);

		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensionsAll.data());

		std::cout << "\navailable extensions:\n";

		for (const auto& extension : extensionsAll)
		{
			std::cout << '\t' << extension.extensionName << '\n';
		}
	}

	bool checkValidationLayerSupport() {
		uint32_t layerOut;
		vkEnumerateInstanceLayerProperties(&layerOut, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerOut);
		vkEnumerateInstanceLayerProperties(&layerOut, availableLayers.data());

		std::cout << "\n available layer properties : \n";
		for (const auto& layerProperties : availableLayers) {
			std::cout << layerProperties.layerName << '\t' << layerProperties.description << '\n';
		}

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

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData) {

		std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

		return VK_FALSE;
	}

	void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;
		createInfo.pUserData = nullptr;
	}

	void setupDebugMessager() {
		if (!enableValidationLayers) return;

		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		populateDebugMessengerCreateInfo(createInfo);

		if (CreateDebugUtilsMessagerEXT(instance, &createInfo, nullptr, &debugMessager) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug messager!");
		}
	}

	void pickPhysicalDevice() {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (deviceCount == 0) {
			throw std::runtime_error("failed to find GPU with Vulkan support");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		std::cout << "\nPhysical Device: \n";
		for (const auto& device : devices) {
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(device, &deviceProperties);

			VkPhysicalDeviceFeatures deviceFeatures;
			vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

			std::cout << " device name= " << deviceProperties.deviceName << "  \n" <<
				" device type= " << deviceProperties.deviceType << "  \n" <<
				" device id= " << deviceProperties.deviceID << "  \n" <<
				" api version= " << deviceProperties.apiVersion << "  \n" <<
				" vendor id= " << deviceProperties.vendorID << "  \n" <<
				"  maxImageDimension2D= " << deviceProperties.limits.maxImageDimension2D << " \n" <<
				"  framebuffer color sample count= " << deviceProperties.limits.framebufferColorSampleCounts << std::endl;

			std::cout << "\n geometryShader= " << deviceFeatures.geometryShader << "\n" <<
				" tessellationShader= " << deviceFeatures.tessellationShader << "\n" <<
				" multiViewport= " << deviceFeatures.multiViewport << "\n" <<
				" shaderFloat64= " << deviceFeatures.shaderFloat64 << "\n" <<
				" textureCompressionASTC_LDR= " << deviceFeatures.textureCompressionASTC_LDR << "\n" <<
				" textureCompressionETC2= " << deviceFeatures.textureCompressionETC2 << "\n" <<
				" textureCompressionBC= " << deviceFeatures.textureCompressionBC << "\n" <<
				" samplerAnisotropy= " << deviceFeatures.samplerAnisotropy << "\n";
		}

		for (const auto& device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				break;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE) {
			throw std::runtime_error("failed to find a suitable GPU!");
		}
	}

	bool isDeviceSuitable(VkPhysicalDevice device) {
		QueueFamilyIndices indices = findQueueFamilies(device);

		return indices.isComplete();
	}

	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;

		bool isComplete() {
			return graphicsFamily.has_value();
		}
	};

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
		QueueFamilyIndices indices;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		std::cout << "\nQueue Families: \n";
		for (const auto& queueFamily : queueFamilies) {
			std::cout << " queueFlags= " << queueFamily.queueFlags << "\n" <<
				" queueCount= " << queueFamily.queueCount << "\n" <<
				" timestampValidBits= " << queueFamily.timestampValidBits << "\n" << std::endl;
		}

		int i = 0;
		for (const auto& queueFamily : queueFamilies) {
			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			if (indices.isComplete()) {
				break;
			}

			i++;
		}

		return indices;
	}
};

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