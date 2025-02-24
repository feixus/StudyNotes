#include <stdio.h>
#include <climits>
#include <cstring>
#include <X11/Xlib-xcb.h>
#include "OpenGL/OpenGLGraphicsManager.hpp"
#include "OpenGLApplication.hpp"
#include "MemoryManager.hpp"

using namespace My;

namespace My {
    GfxConfiguration config(8, 8, 8, 8, 32, 0, 0, 960, 540, "GameEngine(Linux)");
    IApplication* g_pApp                = static_cast<IApplication*>(new OpenGLApplication(config));
    GraphicsManager* g_pGraphicsManager = static_cast<GraphicsManager*>(new OpenGLGraphicsManager);
    MemoryManager* g_pMemoryManager     = static_cast<MemoryManager*>(new MemoryManager);
}

// Helper to check for extension string presence.  Adapted from:
//   http://www.opengl.org/resources/features/OGLextensions/
static bool isExtensionSupported(const char *extList, const char *extension)
{
    const char *start;
    const char *where, *terminator;

    // extension names should not have spaces
    where = strchr(extension, ' ');
    if (where || *extension == '\0')
        return false;
    
    /* it takes a bit of care to be fool-proof about parsing the OpenGL extensions string*/
    for (start = extList;;)
    {
        where = strstr(start, extension);
        // printf(" isExtensionSupported: start = %s\n", start);

        if (!where)
            break;

        terminator = where + strlen(extension);

        if (where == start || *(where - 1) == ' ')
            if (*terminator == ' ' || *terminator == '\0')
                return true;
        
        start = terminator;
    }

    return false;
}

int My::OpenGLApplication::Initialize()
{
    int result;

    int default_screen;
    GLXFBConfig *fb_configs;
    GLXFBConfig fb_config;
    int num_fb_configs = 0;
    XVisualInfo *vi;
    GLXWindow glxwindow;
    const char *glxExts;

    // get a matching FB config
    static int visual_attribs[] =
    {
        GLX_X_RENDERABLE    , True,
        GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
        GLX_RENDER_TYPE     , GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
        GLX_RED_SIZE        , static_cast<int>(INT_MAX & m_Config.redBits),
        GLX_GREEN_SIZE      , static_cast<int>(INT_MAX & m_Config.greenBits),
        GLX_BLUE_SIZE       , static_cast<int>(INT_MAX & m_Config.blueBits),
        GLX_ALPHA_SIZE      , static_cast<int>(INT_MAX & m_Config.alphaBits),
        GLX_DEPTH_SIZE      , static_cast<int>(INT_MAX & m_Config.depthBits),
        GLX_STENCIL_SIZE    , static_cast<int>(INT_MAX & m_Config.stencilBits),
        GLX_DOUBLEBUFFER    , True,
        //GLX_SAMPLE_BUFFERS  , 1,
        //GLX_SAMPLES         , 4,
        None
    };

    /* Open Xlib display */
    m_pDisplay = XOpenDisplay(NULL);
    if (!m_pDisplay)
    {
        fprintf(stderr, "can't open display\n");
        return -1;
    }

    default_screen = DefaultScreen(m_pDisplay);

    gladLoadGLX(m_pDisplay, default_screen);

    /* query framebuffer configurations */
    fb_configs = glXChooseFBConfig(m_pDisplay, default_screen, visual_attribs, &num_fb_configs);
    if (!fb_configs || num_fb_configs == 0)
    {
        fprintf(stderr, "glXGetFBConfigs failed\n");
        return -1;
    }

    /* pick the FB config/visual with the most samplers per pixel */
    {
        int best_fbc = -1, worst_fbc = -1, best_num_samp = -1, worst_num_samp = 999;

        for (int i = 0; i < num_fb_configs; ++i)
        {
            XVisualInfo *vi = glXGetVisualFromFBConfig(display, fb_configs[i]);
            if (vi)
            {
                int samp_buf, samples;
                glXGetFBConfigAttrib(display, fb_configs[i], GLX_SAMPLE_BUFFERS, &samp_buf);
                glXGetFBConfigAttrib(display, fb_configs[i], GLX_SAMPLES, &samples);

                printf ("matching fbconfig %d, visual ID 0x%lx: SAMPLE_BUFFERS = %d, SAMPLES = %d\n",
                        i, vi->visualid, samp_buf, samples);

                if (best_fbc < 0 || (samp_buf && samples > best_num_samp))
                    best_fbc = i, best_num_samp = samples;
                if (worst_fbc < 0 || !samp_buf || samples < worst_num_samp)
                    worst_fbc = i, worst_num_samp = samples;

            }
            XFree(vi);
        }

        fb_config = fb_configs[best_fbc];
    }

    vi = glXGetVisualFromFBConfig(display, fb_config);
    printf("chosen visual ID = 0x%lx\n", vi->visualid);

    /* establish connection to X server */
    m_pConn = XGetXCBConnection(m_pDisplay);
    if (!m_pConn)
    {
        XCloseDisplay(m_pDisplay);
        fprintf(stderr, "cant get xcb connection from display\n");
        return -1;
    }

    /* acquire event queue ownership */
    XSetEventQueueOwner(display, XCBOwnsEventQueue);

    /* find XCB screen */
    xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(xcb_get_setup(m_pConn));
    for (int screen_num = vi->screen; screen_iter.rem && screen_num > 0; --screen_num, xcb_screen_next(&screen_iter));
    m_pScreen = screen_iter.data;
    m_nVi = vi->visualid;

    result = XcbApplication::Initialize();
    if (result) {
        printf("Xcb Application initialized failed\n");
        return -1;
    }

     /* get the default screen's GLX extension list */
     glxExts = glXQueryExtensionsString(m_pDisplay, default_screen);
 
     /* create OpenGL context */
     ctxErrorOccurred = false;
     int (*oldHandler)(Display*, XErrorEvent*) = XSetErrorHandler(&ctxErrorHandler);
 
     if (!isExtensionSupported(glxExts, "GLX_ARB_create_context") || !glXCreateContextAttribsARB)
     {
         printf("glXCreateContextAttribsARB() not found ... using old-style GLX context\n");
 
         m_Context = glXCreateNewContext(m_pDisplay, fb_config, GLX_RGBA_TYPE, 0, True);
         if (!m_Context)
         {
             fprintf(stderr, "glXCreateNewContext failed\n");
             return -1;
         }
     }
     else
     {
         int context_attribs[] = 
         {
             GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
             GLX_CONTEXT_MINOR_VERSION_ARB, 0,
             None
         };
 
         printf("creating context\n");
         m_Context = glXCreateContextAttribsARB(m_pDisplay, fb_config, 0, True, context_attribs);
 
         XSync(display, False);
         if (!ctxErrorOccurred && context)
             printf("created GL 3.0 context\n");
         else
         {
             /* GLX_CONTEXT_MAJOR_VERSION_ARB = 1*/
             context_attribs[1] = 1;
             /* GLX_CONTEXT_MINOR_VERSION_ARB = 0*/
             context_attribs[3] = 0;
 
             ctxErrorOccurred = false;
 
             printf("failed to create GL 3.0 context ... using old-style GLX context\n");
 
             m_Context = glXCreateContextAttribsARB(m_pDisplay, fb_config, 0, True, context_attribs);
         }
     }
 
     XSync(m_pDisplay, False);
 
     XSetErrorHandler(oldHandler);
 
     if (ctxErrorOccurred || !m_Context)
     {
         printf("failed to create an OpenGL context\n");
         return -1;
     }
 
     /* verifying that context is a direct context */
     if (!glXIsDirect(m_pDisplay, m_Context))
     {
         printf("indirect GLX rendering context obtained\n");
     }
     else
     {
         printf("direct GLX rendering context obtained\n");
     }
 
     /* create GLX window */
     glxwindow = glXCreateWindow(m_pDisplay, fb_config, m_Window, 0);
 
     if (!glxwindow)
     {
         xcb_destroy_window(m_pConn, m_Window);
         glXDestroyContext(m_pDisplay, m_Context);
 
         fprintf(stderr, "glXCreateWindow failed\n");
         return -1;
     }
 
     m_Drawable = glxwindow;
 
     /* make OpenGL context current */
     if(!glXMakeContextCurrent(m_pDisplay, m_Drawable, m_Drawable, m_Context))
     {
         xcb_destroy_window(m_pConn, m_Window);
         glXDestroyContext(m_pDisplay, m_Context);
 
         fprintf(stderr, "glXMakeContextCurrent failed\n");
         return -1;
     }

     XFree(vi);

    return result;
}

void My::OpenGLApplication::Finalize()
{
    XcbApplication::Finalize();
}

void My::OpenGLApplication::Tick()
{
    XcbApplication::Tick();
}

void My::OpenGLApplication::OnDraw()
{
    glXSwapBuffers(m_pDisplay, m_Drawable);
}