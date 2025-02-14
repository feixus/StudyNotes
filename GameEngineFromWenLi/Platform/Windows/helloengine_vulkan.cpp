// basic windows header file
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>

#include "vectormath.h"

#include <vulkan/vulkan.hpp>
// #include <vulkan/vulkan_win32.h>

using namespace std;

const uint32_t WIDTH = 1024;
const uint32_t HEIGHT = 512;


typedef struct VertexType
{
    VectorType position;
    VectorType color;
} VertexType;

HDC   g_deviceContext = 0;
HGLRC g_renderingContext = 0;
char  g_videoCardDesription[128];

const bool VSYNC_ENABLED = true;
const float SCREEN_DEPTH = 1000.0f;
const float SCREEN_NEAR = 0.1f;

int g_vertexCount, g_indexCount;
unsigned int g_vertexArrayId, g_vertexBufferId, g_indexBufferId;

unsigned int g_vertexShader;
unsigned int g_fragmentShader;
unsigned int g_shaderProgram;

const char VS_SHADER_SOURCE_FILE[] = "color.vs";
const char PS_SHADER_SOURCE_FILE[] = "color.ps";

float g_positionX = 0, g_positionY = 0, g_positionZ = -10;
float g_rotationX = 0, g_rotationY = 0, g_rotationZ = 0;
float g_worldMatrix[16];
float g_viewMatrix[16];
float g_projectionMatrix[16];

#if defined(_DEBUG)
bool enableValidationLayers = true;
#else
bool enableValidationLayers = false;
#endif

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily; //for drawing commands
	std::optional<uint32_t> presentFamily; //for presentation

	bool isComplete() {
		return graphicsFamily.has_value() && presentFamily.has_value();
	}
};

struct SwapChainSupportDetails {
	vk::SurfaceCapabilitiesKHR capabilities;
	std::vector<vk::SurfaceFormatKHR> formats;
	std::vector<vk::PresentModeKHR> presentModes;
};

vk::Instance g_vkInstance;
vk::SurfaceKHR  g_vkSurface;
vk::PhysicalDevice g_vkPhysicalDevice;
vk::Device  g_vkDevice;
vk::Queue g_vkGraphicQueue;
vk::Queue g_vkPresentQueue;
vk::SwapchainKHR g_vkSwapChain;

std::vector<vk::Image> swapChainImages;
vk::Format swapChainImageFormat;
vk::Extent2D swapchainExtent;
std::vector<vk::ImageView> swapChainImageViews;
vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

vk::RenderPass g_vkRenderPass;
vk::DescriptorSetLayout g_vkDescriptorSetLayout;

std::vector<const char*> getRequiredExtensions() {
	std::vector<const char*> extensions;

    // platform specific extensions
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

    // core extensions
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

	if (enableValidationLayers) {
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

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

static vk::DebugUtilsMessengerCreateInfoEXT getDebugUtilsMessengerCreateInfo() {
    return vk::DebugUtilsMessengerCreateInfoEXT {
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | 
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | 
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
			vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        debugCallback,
    };
}

void createInstance()
{
    vk::ApplicationInfo appInfo {
        "Hello Engine[Vulkan]",     // application name
        VK_MAKE_VERSION(1, 0, 0),   // application version
        "No Engine",                // engine name
        VK_MAKE_VERSION(1, 0, 0),   // engine version
        VK_API_VERSION_1_3          // vulkan api version
    };

    auto extensions = getRequiredExtensions();

    vk::InstanceCreateInfo createInfo {
        {},
        &appInfo,
        static_cast<uint32_t>(extensions.size()),
        extensions.data()
    };

    if (enableValidationLayers) {
        createInfo.setEnabledLayerCount(static_cast<uint32_t>(validationLayers.size()));
        createInfo.setPpEnabledExtensionNames(validationLayers.data());

        auto debugMsgCreateInfo = getDebugUtilsMessengerCreateInfo();
        createInfo.setPNext(&debugMsgCreateInfo);
    }

    try {
        g_vkInstance = vk::createInstance(createInfo);

        std::cout << "vulkan instance -->" << std::endl;
    } catch(const vk::SystemError& e) {
        throw std::runtime_error(std::string("failed to create vulkan instance: ") + e.what());
    }
}

void createSurface(HWND hwnd)
{
    vk::Win32SurfaceCreateInfoKHR  surfaceCreateInfo {
        {},                         // flags
        GetModuleHandle(nullptr),   // hinstance
        hwnd                        // window handlw
    };

    try {
        g_vkSurface = g_vkInstance.createWin32SurfaceKHR(surfaceCreateInfo);
        std::cout << "win32 surface -->" << std::endl;
    } catch(const vk::SystemError& e) {
        throw std::runtime_error(std::string("failed to create win32 surface: ") + e.what());
    }
}

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice physicalDevice)
{
    QueueFamilyIndices indices;

    int i = 0;
    std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
			indices.graphicsFamily = i;

			VkBool32 presentSupport = physicalDevice.getSurfaceSupportKHR(i, g_vkSurface);
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

bool checkDeviceExtensionSupport(vk::PhysicalDevice physicalDevice) 
{
    std::vector<vk::ExtensionProperties> availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}

SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice physicalDevice) {
	SwapChainSupportDetails details;

    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(g_vkSurface);
    details.formats = physicalDevice.getSurfaceFormatsKHR(g_vkSurface);
    details.presentModes = physicalDevice.getSurfacePresentModesKHR(g_vkSurface);

	return details;
}

bool isDeviceSuitable(vk::PhysicalDevice physicalDevice)
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    if (!indices.isComplete()) return false;

    bool extensionsSupported = checkDeviceExtensionSupport(physicalDevice);
    if (!extensionsSupported) return false;

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
    bool swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    if (!swapChainAdequate) return false;

    vk::PhysicalDeviceFeatures supportedFeatures = physicalDevice.getFeatures();
    return supportedFeatures.samplerAnisotropy;
}

void pickPhysicalDevice()
{
	std::vector<vk::PhysicalDevice> devices = g_vkInstance.enumeratePhysicalDevices();
    if (devices.empty()) {
		throw std::runtime_error("failed to find GPU with Vulkan support");
	}

    for (const auto& device : devices) {
		if (isDeviceSuitable(device)) {
			g_vkPhysicalDevice = device;
			break;
		}
	}

    if (!g_vkPhysicalDevice) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void createLogicDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(g_vkPhysicalDevice);

	std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		vk::DeviceQueueCreateInfo queueCreateInfo(
			{},
			queueFamily,
			1,
			&queuePriority
		);
		
		queueCreateInfos.emplace_back(queueCreateInfo);
	}

    vk::PhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

    vk::DeviceCreateInfo createInfo(
        {},
        queueCreateInfos,
        {},
        deviceExtensions,
        &deviceFeatures,
        nullptr
    );

    if (enableValidationLayers) {
        createInfo.setPEnabledLayerNames(validationLayers);
    }

    g_vkDevice = g_vkPhysicalDevice.createDevice(createInfo);
    if (!g_vkDevice) {
		throw std::runtime_error("failed to create logic device!");
	}

    g_vkGraphicQueue = g_vkDevice.getQueue(indices.graphicsFamily.value(), 0);
    g_vkPresentQueue = g_vkDevice.getQueue(indices.presentFamily.value(), 0);
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) 
{
	for (const auto& availableFormat : availableFormats) {
		if (vk::Format::eB8G8R8A8Srgb == availableFormat.format &&
			vk::ColorSpaceKHR::eSrgbNonlinear == availableFormat.colorSpace) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) {
	for (const auto& availablePresentMode : availablePresentModes) {
		if (vk::PresentModeKHR::eMailbox == availablePresentMode) {
			return availablePresentMode;
		}
	}

	return vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != (std::numeric_limits<uint32_t>::max)()) {
		return capabilities.currentExtent;
	}
	else {
		vk::Extent2D actualExtent = { WIDTH, HEIGHT };

		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return actualExtent;
	}
}

void createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(g_vkPhysicalDevice);
    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

    QueueFamilyIndices indices = findQueueFamilies(g_vkPhysicalDevice);

    vk::SwapchainCreateInfoKHR createInfo(
        {},
        g_vkSurface,
        imageCount,
        surfaceFormat.format,
        surfaceFormat.colorSpace,
        extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        nullptr,
        swapChainSupport.capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        presentMode,
        true
    );

    if (indices.graphicsFamily != indices.presentFamily) { 
        createInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
        createInfo.setQueueFamilyIndexCount(2);
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };  
        createInfo.setQueueFamilyIndices(queueFamilyIndices);
    }

    g_vkSwapChain = g_vkDevice.createSwapchainKHR(createInfo);
    if (!g_vkSwapChain) {
		throw std::runtime_error("failed to create swap chain!");
	}

    swapChainImages = g_vkDevice.getSwapchainImagesKHR(g_vkSwapChain);
	swapChainImageFormat = surfaceFormat.format;
	swapchainExtent = extent;
}

vk::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
{
    vk::ImageViewCreateInfo viewInfo{
        {},
        image,
        vk::ImageViewType::e2D,
        format,
        vk::ComponentMapping{},   // Default color mapping
        vk::ImageSubresourceRange{
            aspectFlags,          // Image aspect (color, depth, etc.)
            0, mipLevels,         // Base mip level, level count
            0, 1                  // Base array layer, layer count
        }
    };

    return g_vkDevice.createImageView(viewInfo);
}

void createImageViews() 
{
	swapChainImageViews.resize(swapChainImages.size());
	for (size_t i = 0; i < swapChainImages.size(); i++) {
		swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, vk::ImageAspectFlagBits::eColor, 1);
	}
}

vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) 
{
	for (vk::Format format : candidates)
	{
		vk::FormatProperties props = g_vkPhysicalDevice.getFormatProperties(format);

		if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) {
			return format;
		}
		else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("failed to find supportd format!");
}

vk::Format findDepthFormat() 
{
	return findSupportedFormat(
		{ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
		vk::ImageTiling::eOptimal,
		vk::FormatFeatureFlagBits::eDepthStencilAttachment
	);
}

void createRenderPass() 
{
    vk::AttachmentDescription colorAttachment {
        {},
        swapChainImageFormat,
        msaaSamples,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
    };

    vk::AttachmentReference colorAttachmentRef {
        0,
        vk::ImageLayout::eColorAttachmentOptimal
    };

	vk::AttachmentDescription depthAttachment {
        {},
        findDepthFormat(),
        msaaSamples,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eDontCare,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

	vk::AttachmentReference depthAttachmentRef {
        1,
        vk::ImageLayout::eDepthStencilAttachmentOptimal
    };
	
	vk::AttachmentDescription colorAttachmentResolve {
        {},
        swapChainImageFormat,
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR
    };

	vk::AttachmentReference colorAttachmentResolveRef {
        2,
        vk::ImageLayout::eColorAttachmentOptimal
    };

	vk::SubpassDescription subpass(
        {},
        vk::PipelineBindPoint::eGraphics,
        {},
        colorAttachmentRef,
        colorAttachmentResolveRef,
        &depthAttachmentRef
    );

	std::array<vk::AttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
	
    vk::SubpassDependency dependency{
        VK_SUBPASS_EXTERNAL,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::AccessFlagBits::eColorAttachmentWrite,
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite
    };

    vk::RenderPassCreateInfo renderPassInfo(
        {},
        attachments,
        subpass,
        dependency
    );

    g_vkRenderPass = g_vkDevice.createRenderPass(renderPassInfo);
	if (!g_vkRenderPass) {
		throw std::runtime_error("failed to create render pass!");
	}
}

void createDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding uboLayoutBinding{
        0,
        vk::DescriptorType::eUniformBuffer,
        1,
        vk::ShaderStageFlagBits::eVertex,
        {}
    };

    vk::DescriptorSetLayoutBinding samplerLayoutBinding{
        1,
        vk::DescriptorType::eCombinedImageSampler,
        1,
        vk::ShaderStageFlagBits::eFragment,
        {}
    };

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
	vk::DescriptorSetLayoutCreateInfo layoutInfo(
        {},
        bindings
    );

    g_vkDescriptorSetLayout = g_vkDevice.createDescriptorSetLayout(layoutInfo);
    if (!g_vkDescriptorSetLayout) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

std::vector<char> readFile(const std::string& filename) {
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

void createGraphicsPipeline()
{
    //1.shader info
	auto vertShaderCode = readFile("shaders/vert.spv");
	auto fragShaderCode = readFile("shaders/frag.spv");

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

void InitVulkan(HWND hwnd)
{
    createInstance();
    createSurface(hwnd);
    pickPhysicalDevice();
    createLogicDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
}

bool InitializeOpenGL(HWND hwnd, int screenWidth, int screenHeight, float screenDepth, float screenNear, bool vsync)
{
    return true;
}

bool LoadExtensionList()
{
    return true;
}

void FinalizeOpenGL(HWND hwnd)
{
}

bool InitializeExtensions(HWND hwnd)
{
   
    return true;
}

void OutputShaderErrorMessage(HWND hwnd, unsigned int shaderId, const char* shaderFilename)
{
   
}

void OutputLinkerErrorMessage(HWND hwnd, unsigned int programId)
{
   
}

char* LoadShaderSourceFile(const char* filename)
{
    ifstream fin;
    int fileSize;
    char input;
    char* buffer;

    // open the shader source file 
    fin.open(filename);

    if (fin.fail()) return 0;

    fileSize = 0;

    // read the first element of the file
    fin.get(input);

    // count the number of elements in the text file 
    while(!fin.eof())
    {
        fileSize++;
        fin.get(input);
    }

    fin.close();
    
    buffer = new char[fileSize + 1];
    if (!buffer) return 0;

    fin.open(filename);

    fin.read(buffer, fileSize);

    fin.close();

    buffer[fileSize] = '\0';

    return buffer;
}

bool InitializeShader(HWND hwnd, const char* vsFilename, const char* fsFilename)
{
    
    return true;
}

void ShutdownShader()
{
    
}

bool SetShaderParameters(float* worldMatrix, float* viewMatrix, float* projectionMatrix)
{
    return true;
}

bool InitializeBuffers()
{
     VertexType vertices[] = {
                       {{  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f }},
                       {{  1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }},
                       {{ -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }},
                       {{ -1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f, 0.0f }},
                       {{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f }},
                       {{  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f }},
                       {{ -1.0f, -1.0f, -1.0f }, { 0.5f, 1.0f, 0.5f }},
                       {{ -1.0f, -1.0f,  1.0f }, { 1.0f, 0.5f, 1.0f }},
               };
    uint16_t indices[] = { 1, 2, 3, 3, 2, 6, 6, 7, 3, 3, 0, 1, 0, 3, 7, 7, 6, 4, 4, 6, 5, 0, 7, 4, 1, 0, 4, 1, 4, 5, 2, 1, 5, 2, 5, 6 };

    // set the number of vertices in the vertex array 
    g_vertexCount = sizeof(vertices) / sizeof(VertexType);

    // set the number of indices in the index array 
    g_indexCount = sizeof(indices) / sizeof(uint16_t);

    
    return true;
}

void ShutdownBuffers()
{
    
}

void RenderBuffers()
{
    
}

void CalculateCameraPosition()
{
    VectorType up, position, lookAt;
    float yaw, pitch, roll;
    float rotationMatrix[9];

    // setup the vector that points upwards
    up.x = 0.0f;
    up.y = 1.0f;
    up.z = 0.0f;

    // setup the position of the camera in the world
    position.x = g_positionX;
    position.y = g_positionY;
    position.z = g_positionZ;

    // setup where the camera is looking by default 
    lookAt.x = 0.0f;
    lookAt.y = 0.0f;
    lookAt.z = 1.0f;

    // set the yaw(Y axis), pitch(X axis), and roll(Z axis) rotations in radians 
    pitch = g_rotationX * 0.0174532925f;
    yaw   = g_rotationY * 0.0174532925f;
    roll  = g_rotationZ * 0.0174532925f;

    // create the rotation matrix from the yaw, pitch and roll values 
    MatrixRotationYawPitchRoll(rotationMatrix, yaw, pitch, roll);

    // transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin 
    TransformCoord(lookAt, rotationMatrix);
    TransformCoord(up, rotationMatrix);

    // translate the rotated camera position to the location of the viewer 
    lookAt.x = position.x + lookAt.x;
    lookAt.y = position.y + lookAt.y;
    lookAt.z = position.z + lookAt.z;

    // finally create the view matrix from the three updated vectors 
    BuildViewMatrix(position, lookAt, up, g_viewMatrix);
}

void Draw()
{
   
}


//windowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

//entry point for any windows program
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstances, LPTSTR lpCmdLine, int nCmdShow)
{
    //the handle for the window
    HWND hWnd;
    //holds informatins for the window class
    WNDCLASSEX wc;

    // clear out the window class for use
    ZeroMemory(&wc, sizeof(WNDCLASSEX));

    // fill in the struct with the needed information 
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = _T("Hello, Engine!");

    // register the window class
    RegisterClassEx(&wc);

    // create the window and use the result as the handle
    hWnd = CreateWindowEx(WS_EX_APPWINDOW,
            _T("Hello, Engine!"),
            _T("Hello, Engine!"),
            WS_OVERLAPPEDWINDOW,
            300, 300,
            960, 540,
            NULL, NULL, hInstance, NULL);
    
    InitializeOpenGL(hWnd, 960, 540, SCREEN_DEPTH, SCREEN_NEAR, true);

    // display the window on the screen
    ShowWindow(hWnd, nCmdShow);
    SetForegroundWindow(hWnd);

    InitializeShader(hWnd, VS_SHADER_SOURCE_FILE, PS_SHADER_SOURCE_FILE);

    InitializeBuffers();

    //holds windows event messages
    MSG msg;

    //wait for the next message in the queue, store the result in 'msg'
    while(GetMessage(&msg, NULL, 0, 0))
    {
        //translate keystroke messages into the right format
        TranslateMessage(&msg);

        //send the message to the WindowProc function
        DispatchMessage(&msg);
    }

    ShutdownBuffers();
    ShutdownShader();
    FinalizeOpenGL(hWnd);
   
   //return this part of the WM_QUIT message to Windows
    return msg.wParam;
}

//the main message handler for the program
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    //sort through and find what code to run for the message given
    switch(message)
    {
        case WM_PAINT:
        {
            Draw();
            return 0;
        } break;
        //this message is read when the window is closed
        case WM_DESTROY:
        {
            //close the application entirely
            PostQuitMessage(0);
            return 0;
        } break;
    }

    //handle any messages the switch statement didn't
    return DefWindowProc(hWnd, message, wParam, lParam);
}


/* game engine-13

developer command prompt for vs 2022: 

    cl /EHsc /Z7 opengl32.lib user32.lib gdi32.lib helloengine_opengl.cpp

    debug: devenv /debug helloengine_opengl.exe

cmd:
    clang -o helloengine_opengl helloengine_opengl.cpp -luser32 -lgdi32 -lopengl32

    clang-cl /EHsc -o helloengine_opengl helloengine_opengl.cpp user32.lib gdi32.lib opengl32.lib

*/



