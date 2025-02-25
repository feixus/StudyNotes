#include <string.h>
#include "XcbApplication.hpp"
#include "MemoryManager.hpp"
#include "GraphicsManager.hpp"

using namespace My;

int My::XcbApplication::Initialize()
{
    int result;

    uint32_t mask = 0;
    uint32_t values[3];

    result = BaseApplication::Initialize();

    if (result != 0) exit(result);

    // establish connection to X server
    if (!m_pConn)
        m_pConn = xcb_connect(0, 0);

    // get the first screen
    if (!m_pScreen) {
        m_pScreen = xcb_setup_roots_iterator(xcb_get_setup(m_pConn)).data;
        m_nVi = m_pScreen->root_visual;
    }

    // get the root widow
    m_Window = m_pScreen->root;

    // create XID's for colormap
    xcb_colormap_t colormap = xcb_generate_id(m_pConn);
    
    xcb_create_colormap(m_pConn, XCB_COLORMAP_ALLOC_NONE, colormap, m_Window, m_nVi);

    // create window
    m_Window = xcb_generate_id(m_pConn);
    mask = XCB_CW_COLORMAP | XCB_CW_EVENT_MASK;
    values[0] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;
    values[1] = colormap;
    values[2] = 0;
    xcb_create_window(m_pConn, 
        XCB_COPY_FROM_PARENT, 
        m_Window, 
        m_pScreen->root, 
        20, 20,
        m_Config.screenWidth, m_Config.screenHeight, 
        10, 
        XCB_WINDOW_CLASS_INPUT_OUTPUT, 
        m_nVi, 
        mask, values);

    // set the title of the window
    xcb_change_property(m_pConn, XCB_PROP_MODE_REPLACE, m_Window, 
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 
                        strlen(m_Config.appName), m_Config.appName);

    // set the title of the window icon
    // xcb_change_property(pConn, XCB_PROP_MODE_REPLACE, window, 
    //                     XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8, 
    //                     strlen(title_icon), title_icon);

    // map the window on the screen
    xcb_map_window(m_pConn, m_Window);

    xcb_flush(m_pConn);

    return 0;
}

void My::XcbApplication::Tick()
{
    xcb_generic_event_t *pEvent;
    pEvent = xcb_wait_for_event(m_pConn);
    switch(pEvent->response_type & ~0x80)
    {
        case XCB_EXPOSE:
            {
                OnDraw();
            }
            break;
        case XCB_KEY_PRESS:
            BaseApplication::m_bQuit = true;
            break;
    }

    free(pEvent);
}

void My::XcbApplication::Finalize()
{
    xcb_disconnect(m_pConn);
}

