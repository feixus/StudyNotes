// basic windows header file
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <ranges>
#include <algorithm>

// enable win32 surface extension, to access native platform functions
#define VK_USE_PLATFORM_WIN32_KHR

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_win32.h>
#include <vulkan/vulkan_funcs.hpp>


using namespace std;

const uint32_t WIDTH = 1024;
const uint32_t HEIGHT = 512;

const float SCREEN_DEPTH = 1000.0f;
const float SCREEN_NEAR = 0.1f;

const int MAX_FRAMES_IN_FFLIGHT = 2;

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

vk::Instance m_instance;
vk::DebugUtilsMessengerEXT m_debugUtilsMessenger;
vk::SurfaceKHR  m_surface;

vk::PhysicalDevice m_physicalDevice{};
vk::Device  m_device;

vk::Queue m_graphicQueue;
vk::Queue m_presentQueue;

vk::SwapchainKHR m_swapChain;
std::vector<vk::Image> swapChainImages;
std::vector<vk::ImageView> m_swapChainImageViews;
std::vector<vk::Framebuffer> m_swapChainFramebuffers;
vk::Format swapChainImageFormat;
vk::Extent2D swapchainExtent;
vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

vk::RenderPass m_renderPass;
vk::PipelineLayout m_pipelineLayout;
vk::DescriptorSetLayout g_vkDescriptorSetLayout;
vk::Pipeline m_graphicsPipeline;

vk::CommandPool m_commandPool;
std::vector<vk::CommandBuffer> m_commandBuffers;

std::vector<vk::Semaphore> imageAvailableSemaphores;
std::vector<vk::Semaphore> renderFinishedSemaphores;
std::vector<vk::Fence> inFlightFences;
uint32_t m_currentFrame = 0;

vk::Image colorImage;
vk::DeviceMemory colorImageMemory;
vk::ImageView colorImageView;

vk::Image depthImage;
vk::DeviceMemory depthImageMemory;
vk::ImageView depthImageView;

uint32_t mipLevels;
vk::Image textureImage;
vk::DeviceMemory textureImageMemory;
vk::ImageView textureImageView;
vk::Sampler textureSampler;

// std::vector<Vertex> vertices;
// std::vector<uint32_t> indices;

std::vector<vk::Buffer> uniformBuffers;
std::vector<vk::DeviceMemory> uniformBuffersMemory;
std::vector<void*> uniformBuffersMapped;

vk::Buffer vertexBuffer;
vk::DeviceMemory vertexBufferMemory;
vk::Buffer indexBuffer;
vk::DeviceMemory indexBufferMemory;

vk::DescriptorPool descriptorPool;
std::vector<vk::DescriptorSet> descriptorSets;

bool checkValidationLayerSupport();
std::vector<const char*> getRequiredExtensions();
QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice);
bool checkDeviceExtensionSupport(vk::PhysicalDevice);
SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice);
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>&);
vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>&);
vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR&);
vk::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels);
bool isDeviceSuitable(vk::PhysicalDevice);
vk::Format findDepthFormat();
static std::vector<uint32_t> readFile(const std::string&);
void recreateSwapChain();
vk::ShaderModule createShaderModule(std::vector<uint32_t>&);

static vk::DebugUtilsMessengerCreateInfoEXT getDebugUtilsMessengerCreateInfo();


void createInstance()
{
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

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
        m_instance = vk::createInstance(createInfo);

        std::cout << "vulkan instance -->" << std::endl;
    } catch(const vk::SystemError& e) {
        throw std::runtime_error(std::string("failed to create vulkan instance: ") + e.what());
    }
}

void setupDebugMessenger()
{
    if (!enableValidationLayers) return;

    auto debugMsgCreateInfo = getDebugUtilsMessengerCreateInfo();

    vk::Result result = m_instance.createDebugUtilsMessengerEXT(&debugMsgCreateInfo, nullptr, &m_debugUtilsMessenger);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create debug messenger!");
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
        m_surface = m_instance.createWin32SurfaceKHR(surfaceCreateInfo);
        std::cout << "win32 surface -->" << std::endl;
    } catch(const vk::SystemError& e) {
        throw std::runtime_error(std::string("failed to create win32 surface: ") + e.what());
    }
}

void pickPhysicalDevice()
{
	std::vector<vk::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
    if (devices.empty()) {
		throw std::runtime_error("failed to find GPU with Vulkan support");
	}

    for (const auto& device : devices) {
		if (isDeviceSuitable(device)) {
			m_physicalDevice = device;
			break;
		}
	}

    if (!m_physicalDevice) {
		throw std::runtime_error("failed to find a suitable GPU!");
	}
}

void createLogicDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

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

    m_device = m_physicalDevice.createDevice(createInfo);
    if (!m_device) {
		throw std::runtime_error("failed to create logic device!");
	}

    m_graphicQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
    m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);
}

void createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);
    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    vk::SwapchainCreateInfoKHR createInfo(
        {},
        m_surface,
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

    m_swapChain = m_device.createSwapchainKHR(createInfo);
    if (!m_swapChain) {
		throw std::runtime_error("failed to create swap chain!");
	}

    swapChainImages = m_device.getSwapchainImagesKHR(m_swapChain);
	swapChainImageFormat = surfaceFormat.format;
	swapchainExtent = extent;
}

void createImageViews() 
{
	m_swapChainImageViews.resize(swapChainImages.size());
	for (size_t i = 0; i < swapChainImages.size(); i++) {
		m_swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, vk::ImageAspectFlagBits::eColor, 1);
	}
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

    return m_device.createImageView(viewInfo);
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

    m_renderPass = m_device.createRenderPass(renderPassInfo);
	if (!m_renderPass) {
		throw std::runtime_error("failed to create render pass!");
	}
}

void createGraphicsPipeline()
{
    //1.shader info
	auto vertShaderCode = readFile("shaders/vert.spv");
	auto fragShaderCode = readFile("shaders/frag.spv");

	vk::ShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	vk::ShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo {
        {},
        vk::ShaderStageFlagBits::eVertex,
        vertShaderModule,
        "main"
    };

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo {
        {},
        vk::ShaderStageFlagBits::eFragment,
        fragShaderModule,
        "main"
    };

	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	//2.fixed functions

	//2.1 dynamic state: changes without recreating the pipeline state at draw time
	//eg. the size of viewport / line width / blend constants / depth test / front face ......
	std::vector<vk::DynamicState> dynamicStates{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
	};

	vk::PipelineDynamicStateCreateInfo dynamicState{
        {},
        dynamicStates
    };

	//2.2 vertex input : 
	// bindings -> spacing between data / per-vertex or per-instance
	// attribute descriptions : binding to load them from and at which offset
	//for vertex buffer to do
	// auto bindingDescription = Vertex::getBindingDescription();
	// auto attributeDescriptions = Vertex::getAttributeDescriptions();
	// vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
    //     {},
    //     bindingDescription,
    //     attributeDescriptions
    // );
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
        {},
        {},
        {}
    );

	//2.3 input assembly:
	//		which kind of geometry will be drawn from the vertices
	//		if primitive restart should be enabled -> possible to break up lines and triangles in the _strip topology modes by index of 0xFFFF or 0xFFFFFFFF
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        {},
        vk::PrimitiveTopology::eTriangleList
    };

	//2.4 viewport and scissors:
	//		viewport is the region of the framebuffer, generally (0,0) to (width, height)
	//the size of swap chain and its images may differ from the width and height of the window
	vk::Viewport viewport{
        0, 0,
        (float)swapchainExtent.width, (float)swapchainExtent.height,
        0.0f, 1.0f
    };

	//viewport define the transform from the image to the framebuffer, scissor rectangles define in which region pixels will stored
	vk::Rect2D scissor{ { 0, 0 }, swapchainExtent};

	// viewports and scissor rectangles can either as the static part of pipeline or as dynamic state set in the command buffer

	vk::PipelineViewportStateCreateInfo viewportState(
        {},
        viewport,
        scissor
    );

	//2.5 rasterizer
	//		take the geometry and turn it into fragments
	//		perform depth testing / scissor test / face culling / wireframe rendering
	vk::PipelineRasterizationStateCreateInfo rasterizerStateInfo{
        {},
        VK_FALSE,
        VK_FALSE,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise,
        VK_FALSE,
        0.0f,
        0.0f,
        0.0f,
        1.0f
    };

	//2.6 multisampling
	vk::PipelineMultisampleStateCreateInfo multisamplingStateInfo{
        {},
        msaaSamples,
        VK_FALSE,
        1.0f,
    };
	

	//2.7 depth and stencil test
	vk::PipelineDepthStencilStateCreateInfo depthStencil{
        {},
        VK_TRUE,
        VK_TRUE,
        vk::CompareOp::eLess,
        VK_FALSE,
        VK_FALSE,
        {},
        {},
    };

	//2.8 color blending
	//		mix the old and new one to product a final color / combine the old and new one using a bitwise operation
	//		VkPipelineColorBlendAttachmentState(per attached framebuffer) and VkPipelineColorBlendStateCreateInfo to config color blending
	vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        VK_TRUE,
        vk::BlendFactor::eSrcAlpha,
        vk::BlendFactor::eOneMinusSrc1Alpha,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eOne,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

	vk::PipelineColorBlendStateCreateInfo colorBlendStateInfo(
        {},
        VK_FALSE,
        vk::LogicOp::eCopy,
        colorBlendAttachment,
        {0.0f, 0.0f, 0.0f, 0.0f}
    );

	//2.9 pipeline layout
	// uniform values in shaders, can be changed at drawing time eg. transform matrix / texture samplers
	// vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
    //     {},
    //     g_vkDescriptorSetLayout,
    //     {}
    // );

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
        {},
        {},
        {}
    );

    m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
	if (!m_pipelineLayout) {
		throw std::runtime_error("failed to create pipeline layout!");
	}

	//graphic pipeline: combine shader stages and fix-function and pipeline layout and render pass
    vk::GraphicsPipelineCreateInfo pipelineInfo(
        {},
        2,
        shaderStages,
        &vertexInputInfo,
        &inputAssemblyInfo,
        {},
        &viewportState,
        &rasterizerStateInfo,
        &multisamplingStateInfo,
        &depthStencil,
        &colorBlendStateInfo,
        &dynamicState,
        m_pipelineLayout,
        m_renderPass,
        0,
        VK_NULL_HANDLE,     //create a new graphic pipeline by deriving from an existing pipeline
                            //can reuse and switch quicker, but need pipeline_create_derivative_bit
        -1
    );

	//pipeline cache can across program execution if the cache is stored to a file, can speed up pipeline creation
    auto result = m_device.createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create graphic pipeline!");
    }
    m_graphicsPipeline = result.value;

    m_device.destroyShaderModule(vertShaderModule, nullptr);
    m_device.destroyShaderModule(fragShaderModule, nullptr);
}

void createFramebuffers()
{
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++) {
        std::array<vk::ImageView, 3> attachments = {
            colorImageView,
            depthImageView,
            m_swapChainImageViews[i]
        };

        vk::FramebufferCreateInfo framebufferInfo{
            {},
            m_renderPass,
            static_cast<uint32_t>(attachments.size()),
            attachments.data(),
            swapchainExtent.width,
            swapchainExtent.height,
            1
        };
    
        m_swapChainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
        if (!m_swapChainFramebuffers[i]) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void createCommandPool() 
{
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

    vk::CommandPoolCreateInfo poolInfo{
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        indices.graphicsFamily.value()
    };

    m_commandPool = m_device.createCommandPool(poolInfo);
    if (!m_commandPool) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void createCommandBuffers()
{
    m_commandBuffers.resize(MAX_FRAMES_IN_FFLIGHT);

    vk::CommandBufferAllocateInfo allocInfo(
        m_commandPool,
        vk::CommandBufferLevel::ePrimary,
        (uint32_t)m_commandBuffers.size()
    );

    m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
    if (m_commandBuffers.size() == 0) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void recordCommandBuffer(vk::CommandBuffer commandBuffer, uint32_t imageIndex)
{
    vk::CommandBufferBeginInfo beginInfo{};

    try {
        commandBuffer.begin(beginInfo);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    vk::ClearValue clearColor{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    vk::RenderPassBeginInfo renderPassInfo{
        m_renderPass,
        m_swapChainFramebuffers[imageIndex],
        {
            {0, 0},
            {swapchainExtent.width, swapchainExtent.height}
        },
        clearColor
    };
    
    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

        vk::Viewport viewport{
            0.0f, 0.0f,
            (float)swapchainExtent.width, (float)swapchainExtent.height,
            0.0f, 1.0f
        };

        commandBuffer.setViewport(0, viewport);

        vk::Rect2D scissor{
            {0, 0},
            swapchainExtent
        };

        commandBuffer.setScissor(0, scissor);

        commandBuffer.draw(3, 1, 0, 0);

    commandBuffer.endRenderPass();

    try {
        commandBuffer.end();
    }
    catch (const std::exception& e) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void createSyncObjects()
{
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FFLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FFLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FFLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{
        vk::FenceCreateFlagBits::eSignaled
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
        imageAvailableSemaphores[i] = m_device.createSemaphore(semaphoreInfo);
        renderFinishedSemaphores[i] = m_device.createSemaphore(semaphoreInfo);
        inFlightFences[i] = m_device.createFence(fenceInfo);

        if (!imageAvailableSemaphores[i] || !renderFinishedSemaphores[i] || !inFlightFences[i]) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

vk::ShaderModule createShaderModule(std::vector<uint32_t>& code)
{
    vk::ShaderModuleCreateInfo createInfo(
        {},
        code,
        nullptr
    );

    vk::ShaderModule shaderModule = m_device.createShaderModule(createInfo);
    if (!shaderModule) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

static std::vector<uint32_t> readFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	file.close();
	return buffer;
}

bool checkValidationLayerSupport()
{
    auto availableLayers = vk::enumerateInstanceLayerProperties();
    return std::ranges::all_of(validationLayers, [&availableLayers](const char* layerName) {
        return std::ranges::find_if(availableLayers, [layerName](const vk::LayerProperties& layer) {
            return strcmp(layer.layerName, layerName) == 0;
        }) != availableLayers.end();
    });
}

std::vector<const char*> getRequiredExtensions() {
	std::vector<const char*> extensions;

    // WSI(Window System Integration) extensions enstable connection between vulkan and window system

    // windows platform specific extensions
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    // core extensions
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

	if (enableValidationLayers) {
		extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities) 
{
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

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
	for (const auto& availableFormat : availableFormats) {
        // srgb results in more accurate perceived colors
		if (vk::Format::eB8G8R8A8Srgb == availableFormat.format &&
			vk::ColorSpaceKHR::eSrgbNonlinear == availableFormat.colorSpace) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
{
	for (const auto& availablePresentMode : availablePresentModes) {
        //instead of blocking the application when the queue is full, the mailbox mode will discard the oldest image, "triple buffering"
		if (vk::PresentModeKHR::eMailbox == availablePresentMode) {
			return availablePresentMode;
		}
	}

    // the swap chain is a queue, when it is full, the program has to wait
	return vk::PresentModeKHR::eFifo;
}

SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice physicalDevice)
{
	SwapChainSupportDetails details;

    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(m_surface);
    details.formats = physicalDevice.getSurfaceFormatsKHR(m_surface);
    details.presentModes = physicalDevice.getSurfacePresentModesKHR(m_surface);

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

QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice physicalDevice)
{
    QueueFamilyIndices indices;

    int i = 0;
    std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
			indices.graphicsFamily = i;

            // ensure a device can present images to the surface we created
			VkBool32 presentSupport = physicalDevice.getSurfaceSupportKHR(i, m_surface);
			if (presentSupport) {
				indices.presentFamily = i;
			}
		}

		if (indices.isComplete())
			break;

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

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {

	std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

	return VK_FALSE;
}

static vk::DebugUtilsMessengerCreateInfoEXT getDebugUtilsMessengerCreateInfo()
{
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

vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) 
{
	for (vk::Format format : candidates)
	{
		vk::FormatProperties props = m_physicalDevice.getFormatProperties(format);

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


// void createDescriptorSetLayout()
// {
//     vk::DescriptorSetLayoutBinding uboLayoutBinding{
//         0,
//         vk::DescriptorType::eUniformBuffer,
//         1,
//         vk::ShaderStageFlagBits::eVertex,
//         {}
//     };

//     vk::DescriptorSetLayoutBinding samplerLayoutBinding{
//         1,
//         vk::DescriptorType::eCombinedImageSampler,
//         1,
//         vk::ShaderStageFlagBits::eFragment,
//         {}
//     };

//     std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
// 	vk::DescriptorSetLayoutCreateInfo layoutInfo(
//         {},
//         bindings
//     );

//     g_vkDescriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
//     if (!g_vkDescriptorSetLayout) {
//         throw std::runtime_error("failed to create descriptor set layout!");
//     }
// }

// vk::CommandBuffer beginSingleTimeCommands()
// {
//     vk::CommandBufferAllocateInfo allocInfo{
//         m_commandPool,
//         vk::CommandBufferLevel::ePrimary,
//         1
//     };

//     vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

//     vk::CommandBufferBeginInfo beginInfo{
//         vk::CommandBufferUsageFlagBits::eOneTimeSubmit
//     };

//     commandBuffer.begin(beginInfo);

//     return commandBuffer;
// }

// void endSingleTimeCommands(vk::CommandBuffer commandBuffer)
// {
//     commandBuffer.end();

//     vk::SubmitInfo submitInfo{
//         0,
//         nullptr,
//         nullptr,
//         1,
//         &commandBuffer
//     };

//     m_graphicQueue.submit(1, &submitInfo, nullptr);

//     // a fence can schedule multiple command buffers for execution and wait for all of them to finish
//     m_graphicQueue.waitIdle();

//     m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
// }

// void loadModel(const std::string& path, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
// {
//     tinyobj::attrib_t attrib;
//     std::vector<tinyobj::shape_t> shapes;
//     std::vector<tinyobj::material_t> materials;
//     std::string warn, err;
    
//     if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
//     	throw std::runtime_error(warn + err);
//     }
    
//     std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    
//     for (const auto& shape : shapes) {
//     	for (const auto& index : shape.mesh.indices)
//     	{
//     		Vertex vertex{};
    
//     		//for simplicity, we will assume that every vertex is unique for now, hence the simple auto-increment indices.
//     		vertex.pos = {
//     			attrib.vertices[3 * index.vertex_index + 0],
//     			attrib.vertices[3 * index.vertex_index + 1],
//     			attrib.vertices[3 * index.vertex_index + 2],
//     		};
    
//     		//the OBJ format assumes a coordinate system where a vertical coordinate of 0 means the bottom of the image,
//     		//however we uploaded our image into Vulkan in a top to bottom where 0 means the top of the image.
//     		vertex.texCoord = {
//     			attrib.texcoords[2 * index.texcoord_index + 0],
//     			1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
//     		};
    
//     		vertex.color = { 1.0f, 1.0f, 1.0f };
    
//     		//vertices 11484 -> 3566
//     		if (uniqueVertices.count(vertex) == 0) {
//     			uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
//     			vertices.push_back(vertex);
//     		}
    
//     		indices.push_back(uniqueVertices[vertex]);
//     	}
//     }
// }

// void createImage(uint32_t width, uint32_t height, 
//         uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
//         vk::Format format, vk::ImageTiling tiling,
//         vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
//         vk::Image& image, vk::DeviceMemory& imageMemory)
// {
//     vk::ImageCreateInfo imageInfo{
//         .imageType = vk::ImageType::e2D,
//         .format = format,
//         .extent = {width, height, 1},
//         .mipLevels = mipLevels,
//         .arrayLayers = 1,
//         .samples = numSamples,
//         .tiling = tiling,
//         .usage = usage,
//         .sharingMode = vk::SharingMode::eExclusive,
//         .initialLayout = vk::ImageLayout::eUndefined,
//     };

//     image = m_device.createImage(imageInfo);
//     if (!image) {
//         throw std::runtime_error("failed to create image!");
//     }

//     vk::MemoryRequirements memRequirements = m_device.getImageMemoryRequirements(image);

//     vk::MemoryAllocateInfo allocInfo{
//         .allocationSize = memRequirements.size,
//         .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
//     };

//     imageMemory = m_device.allocateMemory(allocInfo);
//     if (!imageMemory) {
//         throw std::runtime_error("failed to allocate image memory!");
//     }

//     vk::BindImageMemoryInfo bindInfo{
//         .image = image,
//         .memory = imageMemory,
//         .memoryOffset = 0,
//     };
// }

// void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory)
// {
//     vk::BufferCreateInfo bufferInfo{
//         .size = size,
//         .usage = usage,
//         .sharingMode = vk::SharingMode::eExclusive,
//     };

//     buffer = m_device.createBuffer(bufferInfo);
//     if (!buffer) {
//         throw std::runtime_error("failed to create buffer!");
//     }

//     vk::MemoryRequirements memRequirements = m_device.getBufferMemoryRequirements(buffer);

//     vk::MemoryAllocateInfo allocInfo{
//         .allocationSize = memRequirements.size,
//         .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
//     };

//     bufferMemory = m_device.allocateMemory(allocInfo);
//     if (!bufferMemory) {
//         throw std::runtime_error("failed to allocate buffer memory!");
//     }

//     m_device.bindBufferMemory(buffer, bufferMemory, 0);
// }

// bool hasStencilComponent(vk::Format format)
// {
//     return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
// }

// void transitionImageLayout(vk::Image image, vk::Format format, 
//                     vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
//                     uint32_t mipLevels)
// {
//     vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

//     vk::ImageMemoryBarrier barrier{};
//     barrier.oldLayout = oldLayout;
//     barrier.newLayout = newLayout;
//     barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//     barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//     barrier.image = image;
//     barrier.subresourceRange.baseMipLevel = 0;
//     barrier.subresourceRange.levelCount = mipLevels;
//     barrier.subresourceRange.baseArrayLayer = 0;
//     barrier.subresourceRange.layerCount = 1;

//     if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
//         barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

//         if (hasStencilComponent(format)) {
//             barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
//         }
//     }
//     else if {
//         barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
//     }

//     vk::PipelineStageFlags sourceStage;
//     vk::PipelineStageFlags destinationStage;

//     if (oldlayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
//         barrier.srcAccessMask = {};
//         barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

//         sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
//         destinationStage = vk::PipelineStageFlagBits::eTransfer;
//     }
//     else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
//         barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
//         barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

//         sourceStage = vk::PipelineStageFlagBits::eTransfer;
//         destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
//     }
//     else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
//         barrier.srcAccessMask = {};
//         barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

//         sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
//         destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
//     }
//     else {
//         throw std::invalid_argument("unsupported layout transition!");
//     }

//     vk::CmdPipelineBarrier(
//         commandBuffer,
//         sourceStage, destinationStage,
//         {},
//         0, nullptr,
//         0, nullptr,
//         1, &barrier
//     );

//     endSingleTimeCommands(commandBuffer);
// }

// void createColorResources()
// {
//     vk::Format colorFormat = swapChainImageFormat;
//     createImage(swapchainExtent.width, swapchainExtent.height, 
//         1, msaaSamples, 
//         colorFormat, vk::ImageTiling::eOptimal, 
//         vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment, 
//         vk::MemoryPropertyFlagBits::eDeviceLocal, 
//         colorImage, colorImageMemory);
    
//     colorImageView = createImageView(colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
// }

// void createDepthResources()
// {
//     vk::Format depthFormat = findDepthFormat();
//     createImage(swapchainExtent.width, swapchainExtent.height, 
//         1, msaaSamples, 
//         depthFormat, vk::ImageTiling::eOptimal, 
//         vk::ImageUsageFlagBits::eDepthStencilAttachment, 
//         vk::MemoryPropertyFlagBits::eDeviceLocal, 
//         depthImage, depthImageMemory);

//     depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);

//     transitionImageLayout(depthImage, depthFormat, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal, 1);
// }

// void createTextureImage(const char* filename)
// {
//     int texWidth, texHeight, texChannels;
//     stbi_uc* pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
//     vk::DeviceSize imageSize = texWidth * texHeight * 4;

//     if (!pixels) {
//         throw std::runtime_error("failed to load texture image!");
//     }

//     vk::Buffer stagingBuffer;
//     vk::DeviceMemory stagingBufferMemory;
//     createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, 
//         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
//         stagingBuffer, stagingBufferMemory);

//     void* data;
//     m_device.mapMemory(stagingBufferMemory, 0, imageSize, {}, &data);
//     memcpy(data, pixels, static_cast<size_t>(imageSize));
//     m_device.unmapMemory(stagingBufferMemory);

//     stbi_image_free(pixels);

//     createImage(texWidth, texHeight, 1, 
//         vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Srgb, 
//         vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, 
//         vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);

//     transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
//     copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

//     transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

//     m_device.destroyBuffer(stagingBuffer, nullptr);
//     m_device.freeMemory(stagingBufferMemory, nullptr);
// }

// void createTextureImageView()
// {
//     textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels);
// }

// void createTextureSampler()
// {
//     vk::SamplerCreateInfo samplerInfo{
//         .magFilter = vk::Filter::eLinear,
//         .minFilter = vk::Filter::eLinear,
//         .mipmapMode = vk::SamplerMipmapMode::eLinear,
//         .addressModeU = vk::SamplerAddressMode::eRepeat,
//         .addressModeV = vk::SamplerAddressMode::eRepeat,
//         .addressModeW = vk::SamplerAddressMode::eRepeat,
//         .mipLodBias = 0.0f,
//         .anisotropyEnable = VK_TRUE,
//         .maxAnisotropy = 16,
//         .compareEnable = VK_FALSE,
//         .compareOp = vk::CompareOp::eAlways,
//         .minLod = 0.0f,
//         .maxLod = static_cast<float>(mipLevels),
//         .borderColor = vk::BorderColor::eIntOpaqueBlack,
//         .unnormalizedCoordinates = VK_FALSE,
//     };

//     textureSampler = m_device.createSampler(samplerInfo);
//     if (!textureSampler) {
//         throw std::runtime_error("failed to create texture sampler!");
//     }
// }

// void createVertexBuffer()
// {
//     vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

//     vk::Buffer stagingBuffer;
//     vk::DeviceMemory stagingBufferMemory;
//     createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, 
//         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
//         stagingBuffer, stagingBufferMemory);

//     void* data;
//     m_device.mapMemory(stagingBufferMemory, 0, bufferSize, {}, &data);
//     memcpy(data, vertices.data(), (size_t)bufferSize);
//     m_device.unmapMemory(stagingBufferMemory);

//     createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, 
//         vk::MemoryPropertyFlagBits::eDeviceLocal, vertexBuffer, vertexBufferMemory);

//     copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

//     m_device.destroyBuffer(stagingBuffer, nullptr);
//     m_device.freeMemory(stagingBufferMemory, nullptr);
// }

// void createIndexBuffer()
// {
//     vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

//     vk::Buffer stagingBuffer;
//     vk::DeviceMemory stagingBufferMemory;
//     createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, 
//         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
//         stagingBuffer, stagingBufferMemory);

//     void* data;
//     m_device.mapMemory(stagingBufferMemory, 0, bufferSize, {}, &data);
//     memcpy(data, indices.data(), (size_t)bufferSize);
//     m_device.unmapMemory(stagingBufferMemory);

//     createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, 
//         vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer, indexBufferMemory);

//     copyBuffer(stagingBuffer, indexBuffer, bufferSize);

//     m_device.destroyBuffer(stagingBuffer, nullptr);
//     m_device.freeMemory(stagingBufferMemory, nullptr);
// }

// void createUniformBuffers()
// {
//     vk::DeviceSize bufferSize = sizeof(UniformBufferObject);

//     uniformBuffers.resize(MAX_FRAMES_IN_FFLIGHT);
//     uniformBuffersMemory.resize(MAX_FRAMES_IN_FFLIGHT);
//     uniformBuffersMapped.resize(MAX_FRAMES_IN_FFLIGHT);

//     for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
//         createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, 
//             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
//             uniformBuffers[i], uniformBuffersMemory[i]);
    
//         vk::MappedMemoryRange memoryRange{
//             .memory = uniformBuffersMemory[i],
//             .offset = 0,
//             .size = bufferSize,
//         };

//         uniformBuffersMapped[i] = memoryRange;
//     }
// }

// void createDescriptorPool()
// {
//     std::array<vk::DescriptorPoolSize, 2> poolSizes{
//         vk::DescriptorPoolSize{
//             .type = vk::DescriptorType::eUniformBuffer,
//             .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT)
//         },
//         vk::DescriptorPoolSize{
//             .type = vk::DescriptorType::eCombinedImageSampler,
//             .descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT)
//         }
//     };

//     vk::DescriptorPoolCreateInfo poolInfo{
//         .maxSets = static_cast<uint32_t>(swapChainImages.size()),
//         .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
//         .pPoolSizes = poolSizes.data()
//     };

//     descriptorPool = m_device.createDescriptorPool(poolInfo);
//     if (!descriptorPool) {
//         throw std::runtime_error("failed to create descriptor pool!");
//     }
// }

// void createDescriptorSets()
// {
//     std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FFLIGHT, g_vkDescriptorSetLayout);
//     vk::DescriptorSetAllocateInfo allocInfo{
//         .descriptorPool = descriptorPool,
//         .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT),
//         .pSetLayouts = layouts.data()
//     };

//     descriptorSets.resize(MAX_FRAMES_IN_FFLIGHT);
//     descriptorSets = m_device.allocateDescriptorSets(allocInfo);

//     for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
//         vk::DescriptorBufferInfo bufferInfo{
//             .buffer = uniformBuffers[i],
//             .offset = 0,
//             .range = sizeof(UniformBufferObject)
//         };

//         vk::DescriptorImageInfo imageInfo{
//             .sampler = textureSampler,
//             .imageView = textureImageView,
//             .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
//         };

//         std::array<vk::WriteDescriptorSet, 2> descriptorWrites{
//             vk::WriteDescriptorSet{
//                 .dstSet = descriptorSets[i],
//                 .dstBinding = 0,
//                 .dstArrayElement = 0,
//                 .descriptorType = vk::DescriptorType::eUniformBuffer,
//                 .descriptorCount = 1,
//                 .pBufferInfo = &bufferInfo
//             },
//             vk::WriteDescriptorSet{
//                 .dstSet = descriptorSets[i],
//                 .dstBinding = 1,
//                 .dstArrayElement = 0,
//                 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
//                 .descriptorCount = 1,
//                 .pImageInfo = &imageInfo
//             }
//         };

//         m_device.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
//     }
// }

void InitVulkan(HWND hwnd)
{
    createInstance();
    setupDebugMessenger();
    createSurface(hwnd);
    pickPhysicalDevice();
    createLogicDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
	createCommandBuffers();

	createSyncObjects();

    // createDescriptorSetLayout();
    // createColorResources();
    // createDepthResources();
    // createTextureImage(TEXTURE_PATH.c_str());
	// createTextureImageView();
	// createTextureSampler();
    // loadModel(MODEL_PATH.c_str(), vertices, indices);
    // createVertexBuffer();
    // createIndexBuffer();
    // createUniformBuffers();
	// createDescriptorPool();
	// createDescriptorSets();
}

/* wait for the previous frame to finish
   acquire an image from the swap chain
   record a command buffer 
   submit the command buffer to the graphics queue
   present the image to the screen
*/
void Draw()
{
    vk::Result result = m_device.waitForFences(1, &inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fences!");
    }

    uint32_t imageIndex;
    result = m_device.acquireNextImageKHR(m_swapChain, UINT64_MAX, imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapChain();
        return;
    } else if (result != vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    result = m_device.resetFences(1, &inFlightFences[m_currentFrame]);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to reset fences!");
    }

    vk::CommandBuffer commandBuffer = m_commandBuffers[m_currentFrame];
    commandBuffer.reset();
    recordCommandBuffer(commandBuffer, imageIndex);  

    vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[m_currentFrame]};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[m_currentFrame]};
    vk::SubmitInfo submitInfo(
        waitSemaphores,
        waitStages,
        commandBuffer,
        signalSemaphores,
        nullptr
    );

    result = m_graphicQueue.submit(1, &submitInfo, inFlightFences[m_currentFrame]);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    vk::SwapchainKHR swapChains[] = {m_swapChain};
    vk::PresentInfoKHR presentInfo(
        signalSemaphores,
        swapChains,
        imageIndex,
        nullptr
    );

    result = m_graphicQueue.presentKHR(&presentInfo);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        recreateSwapChain();
        return;
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FFLIGHT;
}

void cleanupSwapChain()
{
    for (auto framebuffer : m_swapChainFramebuffers) {
        m_device.destroyFramebuffer(framebuffer);
    }

    for (auto imageView : m_swapChainImageViews) {
        m_device.destroyImageView(imageView);
    }

    m_device.destroyImageView(depthImageView);
    m_device.destroyImage(depthImage);
    m_device.freeMemory(depthImageMemory);

    m_device.destroyImageView(colorImageView);
    m_device.destroyImage(colorImage);
    m_device.freeMemory(colorImageMemory);

    m_device.destroySwapchainKHR(m_swapChain);
}

void recreateSwapChain()
{
    m_device.waitIdle();

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
}

void FinalizeVulkan(HWND hwnd)
{
    createSwapChain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
        m_device.destroySemaphore(imageAvailableSemaphores[i]);
        m_device.destroySemaphore(renderFinishedSemaphores[i]);
        m_device.destroyFence(inFlightFences[i]);
    }

    m_device.freeCommandBuffers(m_commandPool, m_commandBuffers);
    m_device.destroyCommandPool(m_commandPool);

    m_device.destroyPipeline(m_graphicsPipeline);
    m_device.destroyPipelineLayout(m_pipelineLayout);
    m_device.destroyRenderPass(m_renderPass);

    for (auto imageView : m_swapChainImageViews) {
        m_device.destroyImageView(imageView);
    }

    m_device.destroySwapchainKHR(m_swapChain);
    m_device.destroy();

    if (enableValidationLayers) {
        m_instance.destroyDebugUtilsMessengerEXT(m_debugUtilsMessenger, nullptr);
    }

    m_instance.destroySurfaceKHR(m_surface);
    m_instance.destroy();
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
    
    InitVulkan(hWnd);

    // display the window on the screen
    ShowWindow(hWnd, nCmdShow);
    SetForegroundWindow(hWnd);

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

    FinalizeVulkan(hWnd);
   
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

    cl /EHsc /Z7 opengl32.lib user32.lib gdi32.lib helloengine_vulkan.cpp

    debug: devenv /debug helloengine_vulkan.exe

cmd:
    clang -std=c++20 -I"%VULKAN_SDK%/Include" -L"%VULKAN_SDK%/Lib" -fuse-ld=lld -o helloengine_vulkan helloengine_vulkan.cpp -luser32 -lgdi32  -lvulkan-1

    clang-cl /EHsc -o helloengine_vulkan helloengine_vulkan.cpp user32.lib gdi32.lib opengl32.lib

*/



