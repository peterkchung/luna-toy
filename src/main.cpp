// LunaToy - Lunar Simulation by @peterkchung

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>


// ========================================================================================
// Constants, Structs, Helpers
// ========================================================================================

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

constexpr float WORLD_WIDTH = 40.0f;
constexpr float WORLD_HEIGHT = 22.5f;

constexpr float GROUND_HEIGHT = 2.0f;
constexpr float LANDING_PAD_X = 20.0f;
constexpr float LANDING_PAD_WIDTH = 3.0f;
constexpr int TERRAIN_SEGMENTS = 200;

// Physics Sim Constants
constexpr float LUNAR_GRAVITY = 1.62f;       // m/s² — Moon's actual surface gravity
constexpr float THRUST_POWER = 4.0f;         // m/s² — acceleration when thrusting
constexpr float ROTATION_SPEED = 2.5f;       // rad/s
constexpr float INITIAL_FUEL = 100.0f;       // units
constexpr float FUEL_BURN_RATE = 8.0f;       // units/s
constexpr float SAFE_LANDING_VEL = 2.0f;     // m/s — max speed for safe landing
constexpr float SAFE_LANDING_ANGLE = 0.26f;  // ~15 degrees in radians

struct QueueFamilyIndices {
    std::optional<glm::uint32_t> graphicsFamily;
    std::optional<glm::uint32_t> presentFamily;
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct Vertex2D {
    glm::vec2 pos;
    glm::vec3 color;
};

struct PushConstants {
    glm::mat4 mvp;
    glm::vec4 color;
};

struct TerrainVertex {
    glm::vec2 pos;
};

enum class SimState {
    Flying,
    Landed,
    Crashed
};

struct Lander {
    glm::vec2 pos{WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f};  // center of world
    glm::vec2 vel{0.0f, 0.0f};
    float angle = 0.0f;       // radians, 0 = upright
    float fuel = 100.0f;
    bool thrusting = false;
    SimState state = SimState::Flying;
};

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename);
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
};


// ========================================================================================
// Application
// ========================================================================================

class LunaApp {

public:
    void run() {
        initWindow();
        initVulkan();
        initSim();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;
    
    // Vulkan Core
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    
    // Vulkan Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;

    // Render and Frames
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // Pipeline
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline landerPipeline = VK_NULL_HANDLE;    
    VkPipeline terrainPipeline = VK_NULL_HANDLE;

    // Command pool, sync, and buffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    
    // VkBuffer triangleVertexBuffer = VK_NULL_HANDLE;
    // VkDeviceMemory triangleVertexMemory = VK_NULL_HANDLE;
    uint32_t triangleVertexCount = 0;

    bool framebufferResized = false;

    VkBuffer landerVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory landerVertexMemory = VK_NULL_HANDLE;
    uint32_t landerVertexCount = 0;
    
    VkBuffer landingPadVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory landingPadVertexMemory = VK_NULL_HANDLE;
    uint32_t landingPadVertexCount = 0;

    VkBuffer terrainVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory terrainVertexMemory = VK_NULL_HANDLE;
    uint32_t terrainVertexCount = 0;   

    Lander lander;  
    std::vector<glm::vec2> terrainPoints;
    float landingPadX = 0.0f;
    std::vector<StarVertex> stars;
    std::mt19937 rng{42};

    // ------------------------------------------------------------------------------------
    // Main Loop Functions
    // ------------------------------------------------------------------------------------

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Luna", nullptr, nullptr);

        // resize callback to trigger swapchain recreation
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        createPipelineLayout();
        createPipelines();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
        createLanderGeometry();
    }

    void initSim() {
        generateTerrain();
        generateStars();
        createLanderGeometry();
        createTerrainGeometry();
        createStarsGeometry();
        createLandingPadGeometry();
        resetLander();
    }

    void mainLoop() {
        auto lastTime = std::chrono::high_resolution_clock::now();
        
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now-lastTime).count();
            lastTime = now;
            dt = std::min(dt, 0.05f);

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);

            if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
                resetLander();

            // handleInput(dt);
            updatePhysics(dt);
            drawFrame();
        }
        // wait for gpu before cleanup
        vkDeviceWaitIdle(device);
    }

    void cleanup() {
        destroyBuffer(landerVertexBuffer, landerVertexMemory);
        destroyBuffer(landingPadVertexBuffer, landingPadVertexMemory);
         
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        vkDestroyCommandPool(device, commandPool, nullptr);
        cleanupSwapchain();

        vkDestroyPipeline(device, landerPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);

        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    // ------------------------------------------------------------------------------------
    // initWindow functions
    // ------------------------------------------------------------------------------------

    static void framebufferResizeCallback(GLFWwindow* w, int, int) {
        auto app = reinterpret_cast<LunaApp*>(glfwGetWindowUserPointer(w));
        app->framebufferResized = true;
    }

    // ------------------------------------------------------------------------------------
    // initVulkan functions
    // ------------------------------------------------------------------------------------
 
    void createInstance(){
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Luna";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.apiVersion = VK_API_VERSION_1_0;

        uint32_t glfwExtCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = glfwExtCount;
        createInfo.ppEnabledExtensionNames = glfwExts;
        createInfo.enabledLayerCount = 0;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
    }

    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
    }

    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0) throw std::runtime_error("No Vulkan GPU found");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (auto& dev : devices) {
            if (isDeviceSuitable(dev)) {
                physicalDevice = dev;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable GPU found");

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::cout << "GPU: " << props.deviceName << std::endl;
    }

    void createLogicalDevice() {
        auto indices = findQueueFamilies(physicalDevice);
        float priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::vector<uint32_t> uniqueFamilies;
        uniqueFamilies.push_back(indices.graphicsFamily.value());
        if (indices.presentFamily.value() != indices.graphicsFamily.value())
            uniqueFamilies.push_back(indices.presentFamily.value());

        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueCreateInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        VkPhysicalDeviceFeatures supported;
        vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
        if (supported.largePoints) deviceFeatures.largePoints = VK_TRUE;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create logical device");
        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    };

    void createSwapchain() {
        auto support = querySwapchainSupport(physicalDevice);
        auto format = chooseSwapFormat(support.formats);
        auto mode = chooseSwapPresentMode(support.presentModes);
        auto extent = chooseSwapExtent(support.capabilities);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
            imageCount = support.capabilities.maxImageCount;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = format.format;
        createInfo.imageColorSpace = format.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = mode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain");

        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        swapchainImageFormat = format.format;
        swapchainExtent = extent;        
    }

    void createImageViews() {
        swapchainImageViews.resize(swapchainImages.size());
        for (size_t i = 0; i < swapchainImages.size(); i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = swapchainImageFormat;
            viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create image view");
        }
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;    // clear screen at start
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // keep the results
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // ready to display

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create render pass");
    }

    void createFramebuffers() {
        swapchainFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            VkImageView attachments[] = {swapchainImageViews[i]};
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = attachments;
            fbInfo.width = swapchainExtent.width;
            fbInfo.height = swapchainExtent.height;
            fbInfo.layers = 1;
            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("Failed to create framebuffer");
        }
    }
    void createPipelineLayout() {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create pipeline layout");    
    }

    void createPipelines() {
        std::string shaderDir = SHADER_DIR;

        {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(Vertex2D);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::vector<VkVertexInputAttributeDescription> attrs(2);
            attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, pos)};
            attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, color)};

            landerPipeline = createPipeline(
                shaderDir + "/shader.vert.spv", shaderDir + "/shader.frag.spv",
                {binding}, attrs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
            );
        }

        {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(TerrainVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::vector<VkVertexInputAttributeDescription> attrs(1);
            attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};

            terrainPipeline = createPipeline(
                shaderDir + "/terrain.vert.spv", shaderDir + "/terrain.frag.spv",
                {binding}, attrs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
            );
        }

        {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(StarVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::vector<VkVertexInputAttributeDescription> attrs(3);
            attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StarVertex, pos)};
            attrs[1] = {1, 0, VK_FORMAT_R32_SFLOAT, offsetof(StarVertex, brightness)};
            attrs[2] = {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(StarVertex, size)};

            starsPipeline = createPipeline(
                shaderDir + "/stars.vert.spv", shaderDir + "/stars.frag.spv",
                {binding}, attrs, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, true  // blending ON
            );
        }
    }

    void createCommandPool() {
        auto indices = findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create command pool");
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffers");
    }

    void createSyncObjects() {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailableSemaphores[i]);
            vkCreateSemaphore(device, &semInfo, nullptr, &renderFinishedSemaphores[i]);
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
        }
    }

    void generateStars() {
        stars.clear();
        std::uniform_real_distribution<float> xDist(0.0f, WORLD_WIDTH);
        std::uniform_real_distribution<float> yDist(5.0f, WORLD_HEIGHT);
        std::uniform_real_distribution<float> brightDist(0.2f, 1.0f);
        std::uniform_real_distribution<float> sizeDist(1.0f, 3.0f);

        for (int i = 0; i < 300; i++) {
            stars.push_back({
                {xDist(rng), yDist(rng)},
                brightDist(rng),
                sizeDist(rng)
            });
        }
    }

    void createStarsGeometry() {
        starsVertexCount = static_cast<uint32_t>(stars.size());
        VkDeviceSize bufSize = sizeof(StarVertex) * stars.size();
        createBuffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     starsVertexBuffer, starsVertexMemory);
        uploadBuffer(starsVertexBuffer, starsVertexMemory, stars.data(), bufSize);
    }

    void generateTerrain() {
        terrainPoints.clear();
        std::uniform_real_distribution<float> padPosDist(8.0f, WORLD_WIDTH - 8.0f);

        // Randomize landing pad position (away from edges)
        landingPadX = padPosDist(rng);

        float dx = WORLD_WIDTH / TERRAIN_SEGMENTS;

        for (int i = 0; i <= TERRAIN_SEGMENTS; i++) {
            float x = i * dx;
            float height;

            float padLeft = landingPadX - LANDING_PAD_WIDTH / 2.0f;
            float padRight = landingPadX + LANDING_PAD_WIDTH / 2.0f;

            if (x >= padLeft && x <= padRight) {
                // Flat landing zone
                height = 2.0f;
            } else {
                // Layered sine waves — each adds detail at a different scale
                height = 2.0f
                    + 1.5f * std::sin(x * 0.3f)          // broad hills
                    + 0.8f * std::sin(x * 0.7f + 1.0f)   // medium bumps
                    + 0.4f * std::sin(x * 1.5f + 2.0f)   // small ridges
                    + 0.2f * std::sin(x * 3.0f + 0.5f);  // fine texture
                height = std::max(height, 0.5f);          // floor to prevent negative

                // Smooth transition near pad edges (quadratic ease)
                float distToPad = std::min(std::abs(x - padLeft), std::abs(x - padRight));
                if (distToPad < 2.0f) {
                    float t = distToPad / 2.0f;
                    height = glm::mix(2.0f, height, t * t);  // t² = smooth ease-in
                }
            }
            terrainPoints.push_back({x, height});
        }
    }

    void createTerrainGeometry() {
        std::vector<TerrainVertex> verts;
        for (const auto& pt : terrainPoints) {
            verts.push_back({{pt.x, pt.y}});   // surface
            verts.push_back({{pt.x, 0.0f}});   // bottom
        }

        terrainVertexCount = static_cast<uint32_t>(verts.size());
        VkDeviceSize bufSize = sizeof(TerrainVertex) * verts.size();
        createBuffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     terrainVertexBuffer, terrainVertexMemory);
        uploadBuffer(terrainVertexBuffer, terrainVertexMemory, verts.data(), bufSize);
    }

    void createLanderGeometry() {
        float s = 0.5f;
        std::vector<Vertex2D> verts;

        glm::vec3 gold{0.85f, 0.75f, 0.3f};
        glm::vec3 silver{0.7f, 0.72f, 0.75f};
        glm::vec3 dark{0.3f, 0.3f, 0.35f};
        glm::vec3 red{0.9f, 0.2f, 0.1f};

        // Lambda: push 3 vertices scaled by s
        auto addTri = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3 col) {
            verts.push_back({a * s, col});
            verts.push_back({b * s, col});
            verts.push_back({c * s, col});
        };

        // Main body — hexagonal fan from first point
        glm::vec2 bodyPts[] = {
            {-0.6f, 0.0f}, {-0.5f, 0.4f}, {-0.2f, 0.6f},
            {0.2f, 0.6f}, {0.5f, 0.4f}, {0.6f, 0.0f},
            {0.5f, -0.3f}, {-0.5f, -0.3f}
        };
        for (int i = 1; i < 7; i++) {
            addTri(bodyPts[0], bodyPts[i], bodyPts[i + 1], gold);
        }

        // Ascent stage (top silver box)
        glm::vec2 topPts[] = {
            {-0.3f, 0.6f}, {-0.25f, 1.0f}, {0.25f, 1.0f}, {0.3f, 0.6f}
        };
        addTri(topPts[0], topPts[1], topPts[2], silver);
        addTri(topPts[0], topPts[2], topPts[3], silver);

        // Window
        addTri({-0.12f * s, 0.75f * s}, {0.0f, 0.9f * s}, {0.12f * s, 0.75f * s}, dark);

        // Left leg + foot
        addTri({-0.5f, -0.3f}, {-0.9f, -1.0f}, {-0.7f, -1.0f}, dark);
        addTri({-0.9f, -1.0f}, {-1.1f, -1.05f}, {-0.7f, -1.05f}, dark);

        // Right leg + foot
        addTri({0.5f, -0.3f}, {0.7f, -1.0f}, {0.9f, -1.0f}, dark);
        addTri({0.7f, -1.05f}, {0.9f, -1.0f}, {1.1f, -1.05f}, dark);

        // Nozzle
        addTri({-0.15f, -0.3f}, {-0.2f, -0.5f}, {0.2f, -0.5f}, dark);
        addTri({-0.15f, -0.3f}, {0.2f, -0.5f}, {0.15f, -0.3f}, dark);

        // Red marking stripe
        addTri({-0.4f, 0.15f}, {-0.4f, 0.25f}, {0.4f, 0.25f}, red);
        addTri({-0.4f, 0.15f}, {0.4f, 0.25f}, {0.4f, 0.15f}, red);

        landerVertexCount = static_cast<uint32_t>(verts.size());
        VkDeviceSize bufSize = sizeof(Vertex2D) * verts.size();
        createBuffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     landerVertexBuffer, landerVertexMemory);
        uploadBuffer(landerVertexBuffer, landerVertexMemory, verts.data(), bufSize);
    }
    
    void createLandingPadGeometry() {
        float padLeft = landingPadX - LANDING_PAD_WIDTH / 2.0f;
        float padRight = landingPadX + LANDING_PAD_WIDTH / 2.0f;
        float padY = 2.0f;  // matches terrain flat zone height

        std::vector<Vertex2D> verts;
        glm::vec3 padColor{0.2f, 0.8f, 0.2f};

        auto addQuad = [&](float x0, float y0, float x1, float y1, glm::vec3 c) {
            verts.push_back({{x0, y0}, c});
            verts.push_back({{x1, y0}, c});
            verts.push_back({{x1, y1}, c});
            verts.push_back({{x0, y0}, c});
            verts.push_back({{x1, y1}, c});
            verts.push_back({{x0, y1}, c});
        };

        addQuad(padLeft, padY, padRight, padY + 0.1f, padColor);
        addQuad(padLeft - 0.1f, padY, padLeft + 0.1f, padY + 0.8f, padColor);
        addQuad(padRight - 0.1f, padY, padRight + 0.1f, padY + 0.8f, padColor);

        landingPadVertexCount = static_cast<uint32_t>(verts.size());
        VkDeviceSize bufSize = sizeof(Vertex2D) * verts.size();
        createBuffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     landingPadVertexBuffer, landingPadVertexMemory);
        uploadBuffer(landingPadVertexBuffer, landingPadVertexMemory, verts.data(), bufSize);
    }

    void resetLander() {
        lander = Lander{};
    }

    // TEST FUNCTION
    /*
    void createTriangleBuffer() {
        // Three vertices in clip space [-1, 1] with RGB colors
        std::vector<Vertex2D> verts = {
            {{ 0.0f,  0.5f}, {1.0f, 0.0f, 0.0f}},  // top, red
            {{-0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  // bottom-left, green
            {{ 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // bottom-right, blue
        };

        triangleVertexCount = static_cast<uint32_t>(verts.size());
        VkDeviceSize bufSize = sizeof(Vertex2D) * verts.size();

        // HOST_VISIBLE = CPU can write to it. COHERENT = no manual flush needed.
        createBuffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     triangleVertexBuffer, triangleVertexMemory);
        uploadBuffer(triangleVertexBuffer, triangleVertexMemory, verts.data(), bufSize);
    }
    */

    /*
    // ------------------------------------------------------------------------------------
    // handleInput function
    // ------------------------------------------------------------------------------------

    void handleInput(float dt) {
        float moveSpeed = 8.0f;     // world units per second
        float rotateSpeed = 2.5f;   // radians per second

        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            lander.angle -= rotateSpeed * dt;

        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            lander.angle += rotateSpeed * dt;

        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            lander.pos.y += moveSpeed * dt;

        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            lander.pos.y -= moveSpeed * dt;
    }
    */
    
    // ------------------------------------------------------------------------------------
    // updatePhysics function
    // ------------------------------------------------------------------------------------

    void updatePhysics(float dt) {
        if (lander.state != SimState::Flying) return;

        bool thrustInput = glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS ||
                           glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
        bool leftInput = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
        bool rightInput = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

        if (leftInput) lander.angle -= ROTATION_SPEED * dt;
        if (rightInput) lander.angle += ROTATION_SPEED * dt;

        lander.vel.y -= LUNAR_GRAVITY * dt;

        lander.thrusting = thrustInput && lander.fuel > 0.0f;
        if (lander.thrusting) {
            float thrustX = -std::sin(lander.angle) * THRUST_POWER;
            float thrustY =  std::cos(lander.angle) * THRUST_POWER;
            lander.vel.x += thrustX * dt;
            lander.vel.y += thrustY * dt;
            lander.fuel -= FUEL_BURN_RATE * dt;
            lander.fuel = std::max(lander.fuel, 0.0f);
        }

        lander.pos += lander.vel * dt;

        if (lander.pos.x < 0) lander.pos.x += WORLD_WIDTH;
        if (lander.pos.x > WORLD_WIDTH) lander.pos.x -= WORLD_WIDTH;

        // CHANGED: terrain height instead of constant
        float terrainH = getTerrainHeight(lander.pos.x);
        float landerBottom = lander.pos.y - 0.5f;

        if (landerBottom <= terrainH) {
            lander.pos.y = terrainH + 0.5f;

            float speed = glm::length(lander.vel);
            float absAngle = std::abs(std::fmod(lander.angle, glm::two_pi<float>()));
            if (absAngle > glm::pi<float>()) absAngle = glm::two_pi<float>() - absAngle;

            float padLeft = landingPadX - LANDING_PAD_WIDTH / 2.0f;
            float padRight = landingPadX + LANDING_PAD_WIDTH / 2.0f;
            bool onPad = lander.pos.x >= padLeft && lander.pos.x <= padRight;

            if (speed < SAFE_LANDING_VEL && absAngle < SAFE_LANDING_ANGLE && onPad) {
                lander.state = SimState::Landed;
                lander.vel = {0.0f, 0.0f};
                std::cout << "*** SUCCESSFUL LANDING! ***" << std::endl;
                std::cout << "    Speed: " << speed << " m/s  |  Angle: "
                          << glm::degrees(absAngle) << " deg  |  Fuel: "
                          << lander.fuel << std::endl;
            } else {
                lander.state = SimState::Crashed;
                lander.vel = {0.0f, 0.0f};
                if (!onPad)
                    std::cout << "CRASH — Missed the landing pad!" << std::endl;
                else if (speed >= SAFE_LANDING_VEL)
                    std::cout << "CRASH — Too fast! (" << speed << " m/s)" << std::endl;
                else
                    std::cout << "CRASH — Bad angle! (" << glm::degrees(absAngle) << " deg)" << std::endl;
                std::cout << "    Press R to retry." << std::endl;
            }
        }
    }

    float getTerrainHeight(float x) const {
        if (terrainPoints.empty()) return 0.0f;
        float dx = WORLD_WIDTH / TERRAIN_SEGMENTS;
        int idx = static_cast<int>(x / dx);
        idx = std::clamp(idx, 0, static_cast<int>(terrainPoints.size()) - 2);
        float t = (x - terrainPoints[idx].x) / dx;
        t = std::clamp(t, 0.0f, 1.0f);
        return glm::mix(terrainPoints[idx].y, terrainPoints[idx + 1].y, t);
    }

    // ------------------------------------------------------------------------------------
    // drawFrame function
    // ------------------------------------------------------------------------------------

    void drawFrame() {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("Failed to acquire swapchain image");
        }

        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit draw command buffer");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // ------------------------------------------------------------------------------------
    // pickPhysicalDevice helper functions
    // ------------------------------------------------------------------------------------

    bool isDeviceSuitable(VkPhysicalDevice dev) {
        auto indices = findQueueFamilies(dev);
        if (!indices.isComplete()) return false;

        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

        bool swapChainSupported = false;
        for (const auto& ext : available) {
            if (std::string(ext.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                swapChainSupported = true;
                break;
            }
        }

        if (!swapChainSupported) return false;

        auto swapChainSupport = querySwapchainSupport(dev);
        return !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

        for (uint32_t i = 0; i < count; i++) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.graphicsFamily = i;

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);
            if (presentSupport)
                indices.presentFamily = i;

            if (indices.isComplete()) break;
        }
        return indices;
    }

    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice dev) {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);
        if (formatCount > 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, nullptr);
        if (presentModeCount > 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, details.presentModes.data());
        }
        return details;
    }
    
    // ------------------------------------------------------------------------------------
    // swapchain helper functions
    // ------------------------------------------------------------------------------------
    
    VkSurfaceFormatKHR chooseSwapFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return f;
        }
        return formats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) {
        for (auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
        if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        VkExtent2D extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

    // ------------------------------------------------------------------------------------
    // pipeline helper functions
    // ------------------------------------------------------------------------------------

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shader module");
        return module;
    }

    VkPipeline createPipeline(
        const std::string& vertPath, const std::string& fragPath,
        const std::vector<VkVertexInputBindingDescription>& bindings,
        const std::vector<VkVertexInputAttributeDescription>& attributes,
        VkPrimitiveTopology topology,
        bool enableBlending = false
    ) {
        auto vertCode = readFile(vertPath);
        auto fragCode = readFile(fragPath);
        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = swapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // 2D game, draw both sides
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (enableBlending) {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create graphics pipeline");

        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return pipeline;
    }

    // ------------------------------------------------------------------------------------
    // buffer helper functions 
    // ------------------------------------------------------------------------------------

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create buffer");

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate buffer memory");

        vkBindBufferMemory(device, buffer, memory, 0);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        throw std::runtime_error("Failed to find suitable memory type");
    }

    void uploadBuffer(VkBuffer /*buffer*/, VkDeviceMemory memory,
                      const void* data, VkDeviceSize size) {
        void* mapped;
        vkMapMemory(device, memory, 0, size, 0, &mapped);
        memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device, memory);
    }

    void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
    }

    // ------------------------------------------------------------------------------------
    // drawFrame helper functions 
    // ------------------------------------------------------------------------------------

    void recreateSwapchain() {
        // Wait for minimized window
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        while (w == 0 || h == 0) {
            glfwGetFramebufferSize(window, &w, &h);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);
        cleanupSwapchain();

        // Pipelines depend on swapchain extent, so rebuild them too
        vkDestroyPipeline(device, landerPipeline, nullptr);

        createSwapchain();
        createImageViews();
        createPipelines();
        createFramebuffers();
    }    

    void cleanupSwapchain() {
        for (auto fb : swapchainFramebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv : swapchainImageViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = swapchainFramebuffers[imageIndex];
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = swapchainExtent;

        VkClearValue clearColor = {{{0.01f, 0.01f, 0.03f, 1.0f}}};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        float aspect = static_cast<float>(swapchainExtent.width) /
                        static_cast<float>(swapchainExtent.height);
        float halfW = WORLD_WIDTH / 2.0f;
        float halfH = halfW / aspect;
        glm::mat4 proj = glm::ortho(0.0f, WORLD_WIDTH, halfH * 2.0f, 0.0f, -1.0f, 1.0f);

        PushConstants pc{};
        
        // --- Stars (background layer) ---
        if (starsVertexCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, starsPipeline);
            VkBuffer buffers[] = {starsVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

            pc.mvp = proj;
            pc.color = glm::vec4(1.0f);  // full brightness multiplier
            vkCmdPushConstants(cmd, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pc), &pc);
            vkCmdDraw(cmd, starsVertexCount, 1, 0, 0);
        }

        // --- Terrain (back layer) ---
        if (terrainVertexCount > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
            VkBuffer buffers[] = {terrainVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

            pc.mvp = proj;
            pc.color = glm::vec4(0.45f, 0.42f, 0.4f, 1.0f);  // moon gray base
            vkCmdPushConstants(cmd, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pc), &pc);
            vkCmdDraw(cmd, terrainVertexCount, 1, 0, 0);
        }

        // --- 2. Landing pad (middle layer) ---
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, landerPipeline);
            VkBuffer buffers[] = {landingPadVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

            pc.mvp = proj;
            pc.color = glm::vec4(1.0f);
            vkCmdPushConstants(cmd, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pc), &pc);
            vkCmdDraw(cmd, landingPadVertexCount, 1, 0, 0);
        }

        // --- 3. Lander (front layer) ---
        {
            VkBuffer buffers[] = {landerVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(lander.pos, 0.0f));
            model = glm::rotate(model, -lander.angle, glm::vec3(0.0f, 0.0f, 1.0f));

            pc.mvp = proj * model;

            if (lander.state == SimState::Crashed)
                pc.color = glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (lander.state == SimState::Landed)
                pc.color = glm::vec4(0.3f, 1.0f, 0.3f, 1.0f);
            else
                pc.color = glm::vec4(1.0f);

            vkCmdPushConstants(cmd, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pc), &pc);
            vkCmdDraw(cmd, landerVertexCount, 1, 0, 0);
        }

        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer");
    }
};

int main() {
    try {
        LunaApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
