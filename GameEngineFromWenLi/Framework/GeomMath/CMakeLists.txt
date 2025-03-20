IF(${WIN32})
    SET(GEOMMATH_LIB_FILE ${CMAKE_CURRENT_BINARY_DIR}/GeomMath.lib)
    SET(ISPC_COMPILER ${PROJECT_SOURCE_DIR}/External/ispc/bin/ispc.exe)  # add ispc
    SET(ISPC_OPTIONS --arch=x86-64 --target=host)
    SET(LIBRARIAN_COMMAND lib)  #lib may need cmake execute in x64 Native Tools Command Prompt for VS 2022
    SET(LIBRARIAN_OPTIONS /OUT:${GEOMMATH_LIB_FILE})
ELSE(${WIN32})
    SET(GEOMMATH_LIB_FILE ${CMAKE_CURRENT_BINARY_DIR}/libGeomMath.a)
    SET(ISPC_COMPILER ispc)
    SET(ISPC_OPTIONS --arch=x86-64 --target=host)
    SET(LIBRARIAN_COMMAND ar)
    SET(LIBRARIAN_OPTIONS -cr -o ${GEOMMATH_LIB_FILE})
ENDIF(${WIN32})

SET(GEOMMATH_LIB_HEADER_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_custom_command(
    OUTPUT CrossProduct.o
    COMMAND ${ISPC_COMPILER} ${ISPC_OPTIONS} -o CrossProduct.o -I${CMAKE_CURRENT_SOURCE_DIR} -h ${CMAKE_CURRENT_SOURCE_DIR}/include/CrossProduct.h ${CMAKE_CURRENT_SOURCE_DIR}/ispc/CrossProduct.ispc
    COMMENT "Compiling ISPC source"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GEOMMATH_LIB_FILE}
    COMMAND ${LIBRARIAN_COMMAND} ${LIBRARIAN_OPTIONS} CrossProduct.o
    DEPENDS CrossProduct.o
    COMMENT "Creating GeomMath static library"
    VERBATIM
)

# explicitly add ISPC as a dependency to the all target, to ensure ISPC is always built when running make
add_custom_target(
        ISPC ALL
        DEPENDS ${GEOMMATH_LIB_FILE}
)

# IMPORTED -> Prebuild library, so need IMPORTED_LOCATION and INTERFACE_INCLUDE_DIRECTORIES to specify where the precompiled library file
# GLOBAL -> make the library available to all subdirectories in the project
add_library(GeomMath STATIC IMPORTED GLOBAL)
add_dependencies(GeomMath ISPC)

set_target_properties(GeomMath
        PROPERTIES
        IMPORTED_LOCATION ${GEOMMATH_LIB_FILE}
        INTERFACE_INCLUDE_DIRECTORIES ${GEOMMATH_LIB_HEADER_FOLDER}
        )

# the file need deleted when running make clean...
set_directory_properties(
        PROPERTIES
        ADDITIONAL_MAKE_CLEAN_FILES CrossProduct.o
        )