#pragma once

#include <string>
#include <vector>

const uint32_t WIDTH = 1024;
const uint32_t HEIGHT = 512;

const std::string MODEL_PATH = "models/viking_room.obj";
const std::string TEXTURE_PATH = "textures/viking_room.png";

//multiple frames to be in-flight at once. to fix waitting on the previous frame to finish before rendering the next. 
//so any resource that is accessed and modified during rendering must be duplicated. we need multiple command buffers, semephores, fences.
const int MAX_FRAMES_IN_FFLIGHT = 2;

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const  bool enableValidationLayers = false;
#else
const  bool enableValidationLayers = true;
#endif // NDEBUG
