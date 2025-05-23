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

#define STB_IMAGE_IMPLEMENTATION
#include "../../External/stb/stb_image.h"

#include "../../Framework/Common/vectormath.h"


using namespace std;

const uint32_t WIDTH = 1024;
const uint32_t HEIGHT = 512;

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

typedef struct Vertex
{
    VectorType position;
    VectorType color;
    VectorType texCoord;

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.setBinding(0)
                          .setStride(sizeof(Vertex))
                          .setInputRate(vk::VertexInputRate::eVertex);
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        return std::array<vk::VertexInputAttributeDescription, 3>{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position)},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)}
        };
    }

} Vertex;


const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0
};



class VulkanTest
{
public:
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
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createIndexBuffer();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();

        createSyncObjects();
    }

    /* 
    wait for the previous frame to finish
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
        } else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
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

        vk::SubmitInfo submitInfo{};
        submitInfo.setWaitSemaphores(waitSemaphores)
                .setWaitDstStageMask(waitStages)
                .setCommandBuffers(commandBuffer)
                .setSignalSemaphores(signalSemaphores);
        
        result = m_graphicQueue.submit(1, &submitInfo, inFlightFences[m_currentFrame]);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        vk::SwapchainKHR swapChains[] = {m_swapChain};

        vk::PresentInfoKHR presentInfo{};
        presentInfo.setWaitSemaphores(signalSemaphores)
                .setSwapchains(swapChains)
                .setPImageIndices(&imageIndex);

        result = m_presentQueue.presentKHR(&presentInfo);
        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
            recreateSwapChain();
            return;
        } else if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FFLIGHT;
    }

    void FinalizeVulkan()
    {
        m_device.waitIdle();

        cleanupSwapChain();

        m_device.destroySampler(textureSampler);
        m_device.destroyImageView(textureImageView);
        m_device.destroyImage(textureImage);
        m_device.freeMemory(textureImageMemory);

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

        m_device.destroyBuffer(vertexBuffer);
        m_device.freeMemory(vertexBufferMemory);

        m_device.destroyBuffer(indexBuffer);
        m_device.freeMemory(indexBufferMemory);

        m_device.destroy();

        if (enableValidationLayers) {
            auto destroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
            if (destroyDebugUtilsMessengerEXT) {   
                destroyDebugUtilsMessengerEXT(m_instance, m_debugUtilsMessenger, nullptr);
            }
        }

        m_instance.destroySurfaceKHR(m_surface);
        m_instance.destroy();
    }

private:
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
    vk::Pipeline m_graphicsPipeline;

    vk::DescriptorSetLayout m_descriptorSetLayout;
    vk::DescriptorPool descriptorPool;
    std::vector<vk::DescriptorSet> descriptorSets;

    vk::CommandPool m_commandPool;
    std::vector<vk::CommandBuffer> m_commandBuffers;

    std::vector<vk::Semaphore> imageAvailableSemaphores;
    std::vector<vk::Semaphore> renderFinishedSemaphores;
    std::vector<vk::Fence> inFlightFences;
    uint32_t m_currentFrame = 0;

    vk::Image textureImage;
    vk::DeviceMemory textureImageMemory;
    vk::ImageView textureImageView;
    vk::Sampler textureSampler;

    vk::Buffer vertexBuffer;
    vk::DeviceMemory vertexBufferMemory;
    vk::Buffer indexBuffer;
    vk::DeviceMemory indexBufferMemory;

private:
        
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
            VK_API_VERSION_1_0          // vulkan api version
        };

        auto extensions = getRequiredExtensions();

        vk::InstanceCreateInfo createInfo{};
        createInfo.setPApplicationInfo(&appInfo)
                .setPEnabledExtensionNames(extensions);

        if (enableValidationLayers) {
            createInfo.setPEnabledLayerNames(validationLayers);

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

        // load the function pointer for the debug utils messenger
        auto createDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, 
            "vkCreateDebugUtilsMessengerEXT");
        if (!createDebugUtilsMessengerEXT) {
            throw std::runtime_error("failed to get function pointer with debug utils messenger!");
        }

        auto debugMsgCreateInfo = getDebugUtilsMessengerCreateInfo();
        VkDebugUtilsMessengerEXT debugMessenger;
        auto result = createDebugUtilsMessengerEXT(m_instance, 
            reinterpret_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(&debugMsgCreateInfo), 
            nullptr, 
            &debugMessenger);

        if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to create debug messenger!");
        }

        m_debugUtilsMessenger = vk::DebugUtilsMessengerEXT(debugMessenger);
    }

    void createSurface(HWND hwnd)
    {
        vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo{};
        surfaceCreateInfo.setHinstance(GetModuleHandle(nullptr))
                        .setHwnd(hwnd);

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
                //msaaSamples = getMaxUsableSampleCount();
                break;
            }
        }

        if (!m_physicalDevice) {
            throw std::runtime_error("failed to find a suitable GPU!");
        } else if (enableValidationLayers) {
            std::cout << "physical device --> " << std::endl;
            std::cout << "  api version: " << m_physicalDevice.getProperties().apiVersion << std::endl;
            std::cout << "  driver version: " << m_physicalDevice.getProperties().driverVersion << std::endl;
            std::cout << "  vendor ID: " << m_physicalDevice.getProperties().vendorID << std::endl;
            std::cout << "  device ID: " << m_physicalDevice.getProperties().deviceID << std::endl;
            std::cout << "  device name: " << m_physicalDevice.getProperties().deviceName << std::endl;
            std::cout << "  pipeline cache UUID: " << m_physicalDevice.getProperties().pipelineCacheUUID << std::endl;
            std::cout << "  max memory allocation count: " << m_physicalDevice.getProperties().limits.maxMemoryAllocationCount << std::endl;
        }
    }

    void createLogicDevice()
    {
        QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            vk::DeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.setQueueFamilyIndex(queueFamily)
                        .setQueueCount(1)
                        .setPQueuePriorities(&queuePriority);
            queueCreateInfos.emplace_back(queueCreateInfo);
        }

        vk::PhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        vk::DeviceCreateInfo createInfo{};
        createInfo.setQueueCreateInfos(queueCreateInfos)
                .setPEnabledExtensionNames(deviceExtensions)
                .setPEnabledFeatures(&deviceFeatures);

        if (enableValidationLayers) {
            createInfo.setPEnabledLayerNames(validationLayers);
        }

        m_device = m_physicalDevice.createDevice(createInfo);
        if (!m_device) {
            throw std::runtime_error("failed to create logic device!");
        }

        m_graphicQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
        m_presentQueue = m_device.getQueue(indices.presentFamily.value(), 0);

        if (enableValidationLayers) {
            std::cout << "logic device -->" << std::endl;
            std::cout << "  queue families: graphics: " << indices.graphicsFamily.value() << ", present: " << indices.presentFamily.value() << std::endl;
        }
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
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

        vk::SwapchainCreateInfoKHR createInfo{};
        createInfo.setSurface(m_surface)
                .setMinImageCount(imageCount)
                .setImageFormat(surfaceFormat.format)
                .setImageColorSpace(surfaceFormat.colorSpace)
                .setImageExtent(extent)
                .setImageArrayLayers(1)
                .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                .setPreTransform(swapChainSupport.capabilities.currentTransform)
                .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
                .setPresentMode(presentMode)
                .setClipped(true);

        if (indices.graphicsFamily != indices.presentFamily) { 
            createInfo.setImageSharingMode(vk::SharingMode::eConcurrent)
                    .setQueueFamilyIndexCount(2)
                    .setQueueFamilyIndices(queueFamilyIndices);
        } else {
            createInfo.setImageSharingMode(vk::SharingMode::eExclusive);
        }

        m_swapChain = m_device.createSwapchainKHR(createInfo);
        if (!m_swapChain) {
            throw std::runtime_error("failed to create swap chain!");
        }

        swapChainImages = m_device.getSwapchainImagesKHR(m_swapChain);
        swapChainImageFormat = surfaceFormat.format;
        swapchainExtent = extent;

        if (enableValidationLayers) {
            std::cout << "swap chain -->" << std::endl;
            std::cout << "  image count: " << swapChainImages.size() << std::endl;
            std::cout << "  image format: " << vk::to_string(swapChainImageFormat) << std::endl;
            std::cout << "  image extent: width: " << swapchainExtent.width << ", height: " << swapchainExtent.height << std::endl;
        }
    }

    void createImageViews() 
    {
        m_swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            m_swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, vk::ImageAspectFlagBits::eColor, 1);
            if (!m_swapChainImageViews[i]) {
                throw std::runtime_error("failed to create image views!");
            }
        }

        if (enableValidationLayers) {
            std::cout << "swap chain image views --> size: " << m_swapChainImageViews.size() << std::endl;
        }
    }

    vk::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setComponents(vk::ComponentMapping{})
                .setSubresourceRange(vk::ImageSubresourceRange{
                        aspectFlags,          // Image aspect (color, depth, etc.)
                        0, mipLevels,         // Base mip level, level count
                        0, 1                  // Base array layer, layer count
                });

        return m_device.createImageView(viewInfo);
    }

    void createRenderPass() 
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.setFormat(swapChainImageFormat)
                    .setSamples(msaaSamples)
                    .setLoadOp(vk::AttachmentLoadOp::eClear)
                    .setStoreOp(vk::AttachmentStoreOp::eStore)
                    .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                    .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                    .setInitialLayout(vk::ImageLayout::eUndefined)
                    .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.setAttachment(0)
                        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.setFormat(findDepthFormat())
                    .setSamples(msaaSamples)
                    .setLoadOp(vk::AttachmentLoadOp::eClear)
                    .setStoreOp(vk::AttachmentStoreOp::eDontCare)
                    .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                    .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                    .setInitialLayout(vk::ImageLayout::eUndefined)
                    .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.setAttachment(1)
                        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        
        vk::AttachmentDescription colorAttachmentResolve{};
        colorAttachmentResolve.setFormat(swapChainImageFormat)
                            .setSamples(vk::SampleCountFlagBits::e1)
                            .setLoadOp(vk::AttachmentLoadOp::eDontCare)
                            .setStoreOp(vk::AttachmentStoreOp::eStore)
                            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                            .setInitialLayout(vk::ImageLayout::eUndefined)
                            .setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

        vk::AttachmentReference colorAttachmentResolveRef{};
        colorAttachmentResolveRef.setAttachment(2)
                                .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        // vk::SubpassDescription subpass(
        //     {},
        //     vk::PipelineBindPoint::eGraphics,
        //     {},
        //     colorAttachmentRef,
        //     colorAttachmentResolveRef,
        //     &depthAttachmentRef
        // );
        vk::SubpassDescription subpass{};
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setColorAttachments(colorAttachmentRef);

        // std::array<vk::AttachmentDescription, 3> attachments = { colorAttachment, depthAttachment, colorAttachmentResolve };
        std::array<vk::AttachmentDescription, 1> attachments = { colorAttachment };
        
        vk::SubpassDependency dependency{};
        dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
                .setDstSubpass(0)
                .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.setAttachments(attachments)
                    .setSubpasses(subpass)
                    .setDependencies(dependency);

        m_renderPass = m_device.createRenderPass(renderPassInfo);
        if (!m_renderPass) {
            throw std::runtime_error("failed to create render pass!");
        }
        
        if (enableValidationLayers) {
            std::cout << "render pass -->" << std::endl;
            std::cout << "  attachments: " << attachments.size() << std::endl;
            std::cout << "  subpasses: " << " colorAttachmentCount: " << subpass.colorAttachmentCount << ", " 
                                        << " inputAttachmentCount: " << subpass.inputAttachmentCount << ", " 
                                        << " preserveAttachmentCount: " << subpass.preserveAttachmentCount <<std::endl;
            std::cout << "  dependencies: " << " dependencyFlags: " << vk::to_string(dependency.dependencyFlags) << ", "
                                            << " srcSubpass: " << dependency.srcSubpass << ", "
                                            << " dstSubpass: " << dependency.dstSubpass << ", "
                                            << " srcStageMask: " << vk::to_string(dependency.srcStageMask) << ", "
                                            << " dstStageMask: " << vk::to_string(dependency.dstStageMask) << ", "
                                            << " srcAccessMask: " << vk::to_string(dependency.srcAccessMask) << ", "
                                            << " dstAccessMask: " << vk::to_string(dependency.dstAccessMask) << std::endl;
        }
    }

    void createGraphicsPipeline()
    {
        //1.shader info
        auto vertShaderCode = readFile("shaders/vert.spv");
        auto fragShaderCode = readFile("shaders/frag.spv");

        vk::ShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        vk::ShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.setStage(vk::ShaderStageFlagBits::eVertex)
                        .setModule(vertShaderModule)
                        .setPName("main");

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.setStage(vk::ShaderStageFlagBits::eFragment)
                        .setModule(fragShaderModule)
                        .setPName("main");

        vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        //2.fixed functions

        //2.1 dynamic state: changes without recreating the pipeline state at draw time
        //eg. the size of viewport / line width / blend constants / depth test / front face ......
        std::vector<vk::DynamicState> dynamicStates{
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.setDynamicStates(dynamicStates);

        //2.2 vertex input : 
        // bindings -> spacing between data / per-vertex or per-instance
        // attribute descriptions : binding to load them from and at which offset
        //for vertex buffer to do
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.setVertexBindingDescriptionCount(1)
                    .setPVertexBindingDescriptions(&bindingDescription)
                    .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attributeDescriptions.size()))
                    .setPVertexAttributeDescriptions(attributeDescriptions.data());

        //2.3 input assembly:
        //		which kind of geometry will be drawn from the vertices
        //		if primitive restart should be enabled -> possible to break up lines and triangles in the _strip topology modes by index of 0xFFFF or 0xFFFFFFFF
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.setTopology(vk::PrimitiveTopology::eTriangleList)
                        .setPrimitiveRestartEnable(VK_FALSE);

        //2.4 viewport and scissors:
        //		viewport is the region of the framebuffer, generally (0,0) to (width, height)
        //the size of swap chain and its images may differ from the width and height of the window
        vk::Viewport viewport{};
        viewport.setX(0.0f)
                .setY(0.0f)
                .setWidth((float)swapchainExtent.width)
                .setHeight((float)swapchainExtent.height)
                .setMinDepth(0.0f)
                .setMaxDepth(1.0f);

        //viewport define the transform from the image to the framebuffer, scissor rectangles define in which region pixels will stored
        vk::Rect2D scissor{};
        scissor.setOffset(vk::Offset2D(0, 0))
            .setExtent(swapchainExtent);

        // viewports and scissor rectangles can either as the static part of pipeline or as dynamic state set in the command buffer

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.setViewportCount(1)
                    .setPViewports(&viewport)
                    .setScissorCount(1)
                    .setPScissors(&scissor);

        //2.5 rasterizer
        //		take the geometry and turn it into fragments
        //		perform depth testing / scissor test / face culling / wireframe rendering
        vk::PipelineRasterizationStateCreateInfo rasterizerStateInfo{};
        rasterizerStateInfo.setDepthClampEnable(VK_FALSE)
                        .setRasterizerDiscardEnable(VK_FALSE)
                        .setPolygonMode(vk::PolygonMode::eFill)
                        .setCullMode(vk::CullModeFlagBits::eBack)
                        .setFrontFace(vk::FrontFace::eClockwise)     // 顺时针还是逆时针取决于顶点顺序
                        .setDepthBiasEnable(VK_FALSE)
                        .setLineWidth(1.0f)
                        .setDepthBiasEnable(VK_FALSE);

        //2.6 multisampling
        vk::PipelineMultisampleStateCreateInfo multisamplingStateInfo{};
        multisamplingStateInfo.setRasterizationSamples(msaaSamples)
                            .setSampleShadingEnable(VK_FALSE);
        

        //2.7 depth and stencil test
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.setDepthTestEnable(VK_TRUE)
                    .setDepthWriteEnable(VK_TRUE)
                    .setDepthCompareOp(vk::CompareOp::eLess)
                    .setDepthBoundsTestEnable(VK_FALSE)
                    .setStencilTestEnable(VK_FALSE)
                    .setFront(vk::StencilOpState{})
                    .setBack(vk::StencilOpState{})
                    .setMinDepthBounds(0.0f)
                    .setMaxDepthBounds(1.0f);

        //2.8 color blending
        //		mix the old and new one to product a final color / combine the old and new one using a bitwise operation
        //		VkPipelineColorBlendAttachmentState(per attached framebuffer) and VkPipelineColorBlendStateCreateInfo to config color blending
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.setBlendEnable(VK_TRUE)
                            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                            .setColorBlendOp(vk::BlendOp::eAdd)
                            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                            .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
                            .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

        vk::PipelineColorBlendStateCreateInfo colorBlendStateInfo{};
        colorBlendStateInfo.setLogicOpEnable(VK_FALSE)
                        .setLogicOp(vk::LogicOp::eCopy)
                        .setAttachmentCount(1)
                        .setPAttachments(&colorBlendAttachment)
                        .setBlendConstants({0.0f, 0.0f, 0.0f, 0.0f});

        //2.9 pipeline layout
        // uniform values in shaders, can be changed at drawing time eg. transform matrix / texture samplers
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setSetLayoutCount(1)
                        .setPSetLayouts(&m_descriptorSetLayout)
                        .setPushConstantRangeCount(0);

        m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);
        if (!m_pipelineLayout) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        //graphic pipeline: combine shader stages and fix-function and pipeline layout and render pass
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.setStageCount(2)
                    .setPStages(shaderStages)
                    .setPVertexInputState(&vertexInputInfo)
                    .setPInputAssemblyState(&inputAssemblyInfo)
                    .setPViewportState(&viewportState)
                    .setPRasterizationState(&rasterizerStateInfo)
                    .setPMultisampleState(&multisamplingStateInfo)
                    .setPDepthStencilState(&depthStencil)
                    .setPColorBlendState(&colorBlendStateInfo)
                    .setPDynamicState(&dynamicState)
                    .setLayout(m_pipelineLayout)
                    .setRenderPass(m_renderPass)
                    .setSubpass(0)
                    .setBasePipelineHandle(VK_NULL_HANDLE)
                    .setBasePipelineIndex(-1);
    
        //pipeline cache can across program execution if the cache is stored to a file, can speed up pipeline creation
        auto result = m_device.createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to create graphic pipeline!");
        }
        m_graphicsPipeline = result.value;

        m_device.destroyShaderModule(vertShaderModule, nullptr);
        m_device.destroyShaderModule(fragShaderModule, nullptr);

        if (enableValidationLayers) {
            std::cout << "pipeline -->" << std::endl;
            std::cout << "  stageCount: " << pipelineInfo.stageCount << std::endl;
            std::cout << "  renderPass: " << m_renderPass << std::endl;
            std::cout << "  subpass: " << pipelineInfo.subpass << std::endl;
            
        }
    }

    void createFramebuffers()
    {
        m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
        for (size_t i = 0; i < m_swapChainFramebuffers.size(); i++) {
            std::array<vk::ImageView, 1> attachments = {
                m_swapChainImageViews[i]
            };

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.setRenderPass(m_renderPass)
                        .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
                        .setPAttachments(attachments.data())
                        .setWidth(swapchainExtent.width)
                        .setHeight(swapchainExtent.height)
                        .setLayers(1);
        
            m_swapChainFramebuffers[i] = m_device.createFramebuffer(framebufferInfo);
            if (!m_swapChainFramebuffers[i]) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }

        if (enableValidationLayers) {
            std::cout << "framebuffers -->" << std::endl;
            std::cout << "  size: " << m_swapChainFramebuffers.size() << std::endl;
        }
    }

    void createCommandPool() 
    {
        QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                .setQueueFamilyIndex(indices.graphicsFamily.value());

        m_commandPool = m_device.createCommandPool(poolInfo);
        if (!m_commandPool) {
            throw std::runtime_error("failed to create command pool!");
        }

        if (enableValidationLayers) {
            std::cout << "command pool -->" << std::endl;
            std::cout << "  size: " << m_commandPool << std::endl;
        }
    }

    void createVertexBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, 
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
            stagingBuffer, stagingBufferMemory);

        void* data;
        vk::Result result = m_device.mapMemory(stagingBufferMemory, 0, bufferSize, {}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map vertex buffer memory!");
        }

        memcpy(data, vertices.data(), (size_t)bufferSize);
        m_device.unmapMemory(stagingBufferMemory);

        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, 
            vk::MemoryPropertyFlagBits::eDeviceLocal, vertexBuffer, vertexBufferMemory);

        copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

        m_device.destroyBuffer(stagingBuffer, nullptr);
        m_device.freeMemory(stagingBufferMemory, nullptr);
    }

    void createIndexBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, 
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
            stagingBuffer, stagingBufferMemory);

        void* data;
        vk::Result result = m_device.mapMemory(stagingBufferMemory, 0, bufferSize, {}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map index buffer memory!");
        }
        memcpy(data, indices.data(), (size_t)bufferSize);
        m_device.unmapMemory(stagingBufferMemory);

        createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, 
            vk::MemoryPropertyFlagBits::eDeviceLocal, indexBuffer, indexBufferMemory);

        copyBuffer(stagingBuffer, indexBuffer, bufferSize);

        m_device.destroyBuffer(stagingBuffer, nullptr);
        m_device.freeMemory(stagingBufferMemory, nullptr);
    }

    void createTextureImage()
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load("../../Asset/Textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        vk::DeviceSize imageSize = texWidth * texHeight * 4;

        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, 
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, 
            stagingBuffer, stagingBufferMemory);

        void* data;
        vk::Result result = m_device.mapMemory(stagingBufferMemory, 0, imageSize, {}, &data);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to map texture image memory!");
        }
        
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        m_device.unmapMemory(stagingBufferMemory);

        delete[] pixels;

        createImage(texWidth, texHeight, 1, 
            vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Srgb, 
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, 
            vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);

        transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
        
        copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));

        transitionImageLayout(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

        m_device.destroyBuffer(stagingBuffer, nullptr);
        m_device.freeMemory(stagingBufferMemory, nullptr);
    }

    void createTextureImageView()
    {
        textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, 1);
    }

    void createTextureSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.setMagFilter(vk::Filter::eLinear)
                .setMinFilter(vk::Filter::eLinear)
                .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                .setMipLodBias(0.0f)
                .setAnisotropyEnable(VK_TRUE)
                .setMaxAnisotropy(16)
                .setCompareEnable(VK_FALSE)
                .setCompareOp(vk::CompareOp::eAlways)
                .setMinLod(0.0f)
                .setMaxLod(static_cast<float>(1))
                .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
                .setUnnormalizedCoordinates(VK_FALSE);

        textureSampler = m_device.createSampler(samplerInfo);
        if (!textureSampler) {
            throw std::runtime_error("failed to create texture sampler!");
        }
    }

    void createCommandBuffers()
    {
        m_commandBuffers.resize(MAX_FRAMES_IN_FFLIGHT);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.setCommandPool(m_commandPool)
                .setLevel(vk::CommandBufferLevel::ePrimary)
                .setCommandBufferCount((uint32_t)m_commandBuffers.size());

        m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
        if (m_commandBuffers.size() == 0) {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        if (enableValidationLayers) {
            std::cout << "command buffers -->" << std::endl;
            std::cout << "  size: " << m_commandBuffers.size() << std::endl;
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

        std::array<vk::ClearValue, 2> clearValues{
            vk::ClearColorValue(std::array<float, 4>{0.2f, 0.3f, 0.0f, 1.0f}),
            vk::ClearDepthStencilValue(1.0f, 0)
        };

        vk::Rect2D renderArea(
            vk::Offset2D(0, 0),
            vk::Extent2D(swapchainExtent.width, swapchainExtent.height)
        );

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.setRenderPass(m_renderPass)
                    .setFramebuffer(m_swapChainFramebuffers[imageIndex])
                    .setRenderArea(renderArea)
                    .setClearValues(clearValues);
        
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

            vk::Buffer vertexBuffers[] = {vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, descriptorSets[m_currentFrame], nullptr);

            commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        commandBuffer.endRenderPass();

        try {
            commandBuffer.end();
        }
        catch (const std::exception& e) {
            throw std::runtime_error("failed to record command buffer!");
        }
    }

    void createDescriptorSetLayout()
    {
        // combined image sampler
        vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.setBinding(0)
                            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                            .setDescriptorCount(1)
                            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
                            .setPImmutableSamplers(nullptr);

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        std::array<vk::DescriptorSetLayoutBinding, 1> bindings = { samplerLayoutBinding };
        layoutInfo.setBindings(bindings);

        m_descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
        if (!m_descriptorSetLayout) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].setType(vk::DescriptorType::eCombinedImageSampler)
                    .setDescriptorCount(static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT));

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.setMaxSets(static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT))
                .setPoolSizes(poolSizes);

        descriptorPool = m_device.createDescriptorPool(poolInfo);
        if (!descriptorPool) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    void createDescriptorSets()
    {
        std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FFLIGHT, m_descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.setDescriptorPool(descriptorPool)
                .setDescriptorSetCount(static_cast<uint32_t>(MAX_FRAMES_IN_FFLIGHT))
                .setPSetLayouts(layouts.data());

        descriptorSets = m_device.allocateDescriptorSets(allocInfo);
        if (descriptorSets.size() == 0) {
            throw std::runtime_error("failed to allocate descriptor sets!");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FFLIGHT; i++) {
            vk::DescriptorImageInfo imageInfo{};
            imageInfo.setSampler(textureSampler)
                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setImageView(textureImageView);

            vk::WriteDescriptorSet descriptorWrite_image{};
            descriptorWrite_image.setDstSet(descriptorSets[i])
                                .setDstBinding(0)
                                .setDstArrayElement(0)
                                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                                .setDescriptorCount(1)
                                .setPImageInfo(&imageInfo);

            std::array<vk::WriteDescriptorSet, 1> descriptorWrites{
                descriptorWrite_image
            };

            m_device.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
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

    void copyBufferToImage(vk::Buffer buffer, vk::Image image, uint32_t width, uint32_t height)
    {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

        vk::BufferImageCopy region{};
        region.setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1})
            .setImageOffset({0, 0, 0})
            .setImageExtent({width, height, 1});

        commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

        endSingleTimeCommands(commandBuffer);
    }

    void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size)
    {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

        vk::BufferCopy copyRegion{};
        copyRegion.setSrcOffset(0)
                .setDstOffset(0)
                .setSize(size);

        commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);
        
        endSingleTimeCommands(commandBuffer);
    }

    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory)
    {
        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.setSize(size)
                .setUsage(usage)
                .setSharingMode(vk::SharingMode::eExclusive);

        buffer = m_device.createBuffer(bufferInfo);
        if (!buffer) {
            throw std::runtime_error("failed to create buffer!");
        }

        vk::MemoryRequirements memRequirements = m_device.getBufferMemoryRequirements(buffer);

        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.setAllocationSize(memRequirements.size)
                .setMemoryTypeIndex(findMemoryType(memRequirements.memoryTypeBits, properties));

        bufferMemory = m_device.allocateMemory(allocInfo);
        if (!bufferMemory) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        m_device.bindBufferMemory(buffer, bufferMemory, 0);
    }

    vk::CommandBuffer beginSingleTimeCommands()
    {
        vk::CommandBufferAllocateInfo allocInfo{
            m_commandPool,
            vk::CommandBufferLevel::ePrimary,
            1
        };

        vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };

        commandBuffer.begin(beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(vk::CommandBuffer commandBuffer)
    {
        commandBuffer.end();

        vk::SubmitInfo submitInfo{
            0,
            nullptr,
            nullptr,
            1,
            &commandBuffer
        };

        vk::Result result = m_graphicQueue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to submit single time command buffer!");
        }

        // a fence can schedule multiple transfer simultaneously and wait for all of them complete, instead of executing one at a time.
        m_graphicQueue.waitIdle();

        m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
    }

    bool hasStencilComponent(vk::Format format)
    {
        return format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint;
    }

    // using image memory barrier to perform layout transition
    void transitionImageLayout(vk::Image image, vk::Format format, 
                        vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                        uint32_t mipLevels)
    {
        vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;

            if (hasStencilComponent(format)) {
                barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
        }
        else {
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        }

        vk::PipelineStageFlags sourceStage;
        vk::PipelineStageFlags destinationStage;

        if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            sourceStage = vk::PipelineStageFlagBits::eTransfer;
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
        }
        else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        }
        else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        commandBuffer.pipelineBarrier(
            sourceStage, destinationStage,
            {},
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        endSingleTimeCommands(commandBuffer);
    }

    //memoryType
    //memoryHeaps: memory resources like dedicated VRAM and swap space in RAM for when VRAM runs out.
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
    {
        vk::PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if (typeFilter & (1 << i) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw std::runtime_error("failed to find suitable memory type!");
    }

    void createImage(uint32_t width, uint32_t height, 
            uint32_t mipLevels, vk::SampleCountFlagBits numSamples,
            vk::Format format, vk::ImageTiling tiling,
            vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
            vk::Image& image, vk::DeviceMemory& imageMemory)
    {
        vk::ImageCreateInfo imageInfo{
            {},
            vk::ImageType::e2D,
            format,
            {width, height, 1},
            mipLevels,
            1,
            numSamples,
            tiling,
            usage,
            vk::SharingMode::eExclusive,
            {},
            vk::ImageLayout::eUndefined,
        };

        image = m_device.createImage(imageInfo);
        if (!image) {
            throw std::runtime_error("failed to create image!");
        }

        vk::MemoryRequirements memRequirements = m_device.getImageMemoryRequirements(image);

        vk::MemoryAllocateInfo allocInfo{
            memRequirements.size,
            findMemoryType(memRequirements.memoryTypeBits, properties),
        };

        imageMemory = m_device.allocateMemory(allocInfo);
        if (!imageMemory) {
            throw std::runtime_error("failed to allocate image memory!");
        }

        m_device.bindImageMemory(image, imageMemory, 0);
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

    vk::SampleCountFlagBits getMaxUsableSampleCount()
    {
        vk::PhysicalDeviceProperties physicalDeviceProperties = m_physicalDevice.getProperties();

        vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
        if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
        if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
        if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
        if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
        if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

        return vk::SampleCountFlagBits::e1;
    }

    bool checkValidationLayerSupport()
    {
        auto availableLayers = vk::enumerateInstanceLayerProperties();
        return ::std::ranges::all_of(validationLayers, [&availableLayers](const char* layerName) {
            return ::std::ranges::find_if(availableLayers, [layerName](const vk::LayerProperties& layer) {
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

    void cleanupSwapChain()
        {
            for (auto framebuffer : m_swapChainFramebuffers) {
                m_device.destroyFramebuffer(framebuffer);
            }

            for (auto imageView : m_swapChainImageViews) {
                m_device.destroyImageView(imageView);
            }

            m_device.destroySwapchainKHR(m_swapChain);
        }

    void recreateSwapChain()
    {
        m_device.waitIdle();

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createFramebuffers();
    }
};


VulkanTest vulkanTest;

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
    
    
    vulkanTest.InitVulkan(hWnd);

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

    vulkanTest.FinalizeVulkan();
   
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
            vulkanTest.Draw();
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

    cl /EHsc /Z7 /MDd /D_DEBUG -I"%VULKAN_SDK%/Include" user32.lib gdi32.lib vulkan-1.lib helloengine_vulkan.cpp -std:c++20   /link /LIBPATH:"%VULKAN_SDK%/Lib"   

    debug: devenv /debug helloengine_vulkan.exe

cmd:
    clang -std=c++20 -g -D_DEBUG -I"%VULKAN_SDK%/Include" -L"%VULKAN_SDK%/Lib" -fuse-ld=lld -o helloengine_vulkan.exe helloengine_vulkan.cpp -luser32 -lgdi32  -lvulkan-1 -lmsvcrtd


    normal error can be solved by validation layers, but when the program runs without any problems, 
    but there is no drawing, you can use RenderDoc to debug, check the parameters of each resource.

    vkAlloateMemory: to allocate memory for a large number of objects at the same time is to create a custom allocator or use VulkanMemoryAllocator library.

*/



