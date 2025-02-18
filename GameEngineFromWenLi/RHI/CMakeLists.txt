#Empty
add_subdirectory(Empty)
add_subdirectory(OpenGL)
IF(${WIN32})
        add_subdirectory(D3d)
ENDIF(${WIN32})