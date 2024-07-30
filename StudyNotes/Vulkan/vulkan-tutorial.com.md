## overview
- how to draw a triangle
  - Instance and physical device selection
  VkInstance | VkPhysicalDevice

  - Logic device and queue families
  VkDevice | VkQueue

  - Window surface and swap chain
  create a window to present rendered images to.
  render to a window with VkSurfaceKHR | VkSwapchainKHR
  VkSurfaceKHR: vulkan api is completely platform agnostic, so to use the standardized WSI(Window System Interface) extension to interact with the window manager.
                the surface is a cross-platform abstraction over window to render to
                generally instantiated by providing a reference to the native window handle.
  VKSwapchainKHR: swap chain is a collection of render targets. to ensure the image that currently rendering to is different from the one that is currently on the screen.
                  draw a frame we have to ask the swap chain to provide us with an image to render to. when finished drawing a frame, the image return to the swap chain, then to be present to the screen at some point.
                  the number of render target and conditions for presenting finished images to the screen depend on the present mode(eg. double buffering(vsync) or triple buffering). 

  - Image views and framebuffers
  to draw to an image acquired from the swap chain, warp it into a VkImageView and VkFramebuffer. 
  an image view reference a specific part of an image to be used.
  a framebuffer references image views that are to be used for color,depth,and stencil targets.

  - Render passes
  describe the type of images, how they will be used, how their contents should be treated. VkFramebuffer actually binds specific images to these slots.

  - Graphic pipeline
  VkPipeline object. 
  describe configurable state of the graphics card, like the viewport size and depth buffer operation and the programmable state using VkShaderModule objects.
  VkShaderModule objects are created from shader byte code. 
  the driver alse needs to know which render targets will be used in the pipeline, which we specify by referencing the render pass.
  have to create many VkPipeline objects in advance for all the different combinations you need for your rendering operations.
  only some basic configuration, like viewport size and clear color, can be changed dynamically.

  - Command pools and command buffers
  VkCommandBuffer | VkCommandPool

  - Main loop
  first acquire an image from swap chain with vkAcquireNextImageKHR. then select the appropriate command buffer for the image and execute it with vkQueueSubmit.
  finally, return the image to the swap chain for presentation to the screen with vkQueuePresentKHR.
  operations that are submitted to queues are executed asynchronously. have to use synchronization objects like semaphores to ensure a correct order of execution.
  execution of the draw command buffer must be set up to wait on image acqussition to finish.
  the vkQueuePresentKHR call in turn needs to wait for rendering to be finished, for which use a second semaphore that is signaled after rendering completes.

  - validation layers
  do things like running extra checks on function parameters and tracking memory management problems.

### Frame buffers
    在render pass创建期间指定的attachments,通过将其封装进VkFramebuffer对象中来绑定. 一个framebuffer object引用所有表达此attachments的VkImageView objects.

### Command buffers
    将所有需执行的操作记录到command buffer objects. 所有的commands可以一起提交， Vulkan能更有效率的处理这些commands,允许在多线程记录command.
    VkCommandPool | vkCreateCommandPool | vkDestroyCommandPool
    command buffers通过提交它们至device queue中的一个来执行. 每个command pool 仅分配提交到单个类型queue的command buffers.
    VkCommandBuffer | vkAllocateCommandBuffers
    vkBeginCommandBuffer
    vkCmdBeginRenderPass
    vkCmdBindPipeline
    vkCmdSetViewport | vkCmdSetScissor (可动态设置pipeline的viewport state和scissor state)
    vkCmdDraw
    vkCmdEndRenderPass
    vkEndCommandBuffer

### Rendering and Presentation
##### outline of a frame
等待前一帧结束
向swap chain请求一张图片
记录一个command buffer, 用来绘制场景至图片
提交 recorded command buffer
present the swap chain image
##### synchronization
GPU上的同步执行是显式的。操作的顺序需使用各种synchronization primitives来告知驱动我们想要运行的顺序。因许多Vulkan API的调用在GPU上是异步执行的， 函数的返回早于其操作结束。
这里需要一些order explicitly, 因在GPU上的异步执行:
    acquire an image from the swap chain
    execute commands that draw onto the acquired image
    present that image to the screen for presentation, returning it to the swapchain

##### semaphores
semaphore用于在queue operation之间添加顺序.既可以是同一个queue，也可以是不同queues.
vulkan中有两种类型的semaphores: binary 和 timeline
binary semaphore可以是unsignaled或signaled. 初始为unsignaled.如vkQueueSubmit,但这些都是是GPU waiting.
VkSemaphore | vkCreateSemaphore | vkDestroySemaphore

##### fences
CPU wait, CPU上的顺序执行，也是用于synchronize execution.
Fences必须手动重置以回到unsignaled状态.因fences用于控制host的执行，host可以决定何时重置fence.
fences用于保持CPU和GPU彼此同步.
VkFence | vkCreateFence | vkDestroyFence