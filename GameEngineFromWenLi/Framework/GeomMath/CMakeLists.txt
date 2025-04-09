
SET(GEOMMATH_LIB_FILE ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}GeomMath${CMAKE_STATIC_LIBRARY_SUFFIX})
IF(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    SET(ISPC_COMPILER ispc.exe)  # add ispc
    SET(ISPC_COMPILER_PATH ${PROJECT_SOURCE_DIR}/External/ispc/bin/)
    IF(NOT ${CMAKE_CROSSCOMPILING})
        IF(${ARCH_BIT} MATCHES "64")
            SET(ISPC_OPTIONS --arch=x86-64 --target=host)
            SET(CMAKE_AR lib)
            SET(ISPC_LIBRARIAN_OPTIONS /MACHINE:X64 /OUT:${GEOMMATH_LIB_FILE})  
        else()
            SET(ISPC_OPTIONS --arch=x86)
            SET(CMAKE_AR lib) 
            SET(ISPC_LIBRARIAN_OPTIONS /MACHINE:X86 /OUT:${GEOMMATH_LIB_FILE})
        endif()
    ELSE()
        # build for ARM on an x86_64 machine / building for Windows on a Linux machine ...
    ENDIF()
ELSEIF(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    SET(ISPC_COMPILER ispc)
    SET(ISPC_COMPILER_PATH ${PROJECT_SOURCE_DIR}/External/ispc/)
    SET(ISPC_OPTIONS --arch=x86-64)
    SET(ISPC_LIBRARIAN_OPTIONS -rcv -o ${GEOMMATH_LIB_FILE})
ENDIF()

SET(GEOMMATH_LIB_HEADER_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)

# IMPORTED -> Prebuild library, so need IMPORTED_LOCATION and INTERFACE_INCLUDE_DIRECTORIES to specify where the precompiled library file
# GLOBAL -> make the library available to all subdirectories in the project
add_library(GeomMath STATIC IMPORTED GLOBAL)
add_dependencies(GeomMath GeomMathLib)

set_target_properties(GeomMath
        PROPERTIES
        IMPORTED_LOCATION ${GEOMMATH_LIB_FILE}
        INTERFACE_INCLUDE_DIRECTORIES ${GEOMMATH_LIB_HEADER_FOLDER}
        )

add_subdirectory(ispc)
add_subdirectory(test)