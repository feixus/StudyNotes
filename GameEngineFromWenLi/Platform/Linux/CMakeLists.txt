add_executable(MyGameEngineOpenGL
    XcbApplication.cpp
    OpenGLApplication.cpp
    ${PROJECT_SOURCE_DIR}/External/src/glad/src/glad_glx.c)
target_link_libraries(MyGameEngineOpenGL Common OpenGLRHI xcb X11 X11-xcb dl)
target_include_directories(MyGameEngineOpenGL PRIVATE "${PROJECT_SOURCE_DIR}/External/src/glad/include")