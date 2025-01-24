// basic windows header file
#include <windows.h>
#include <windowsx.h>
#include <tchar.h>

#include <GL/gl.h>
#include <fstream>
#include <iostream>

#include "math.h"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_win32.h>

using namespace std;

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

vk::Instance g_vkInstance;

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

void CreateInstance()
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

void InitVulkan()
{

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



