set(FUNCTIONS
    MulByElement
    CrossProduct
    DotProduct
    Transpose
    Normalize
    Transform
    AddByElement
    SubByElement
)

foreach(FUNC IN LISTS FUNCTIONS)
    add_custom_command(OUTPUT ${FUNC}.o
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${ISPC_COMPILER_PATH}" ${ISPC_COMPILER} ${ISPC_OPTIONS} -o ${FUNC}.o -h ${GEOMMATH_LIB_HEADER_FOLDER}/${FUNC}.h --target=host ${CMAKE_CURRENT_SOURCE_DIR}/${FUNC}.ispc
    MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/${FUNC}.ispc
    COMMENT "Compiling ISPC source: ${FUNC}"
    )

    list(APPEND OBJECTS ${FUNC}.o)

endforeach(FUNC)

add_custom_command(OUTPUT ${GEOMMATH_LIB_FILE}
        COMMAND ${CMAKE_AR} ${ISPC_LIBRARIAN_OPTIONS} ${OBJECTS}
        #COMMAND rm -v ${OBJECTS}
        DEPENDS ${OBJECTS}
        )

# explicitly add ISPC as a dependency to the all target, to ensure ISPC is always built when running make
add_custom_target(GeomMathLib ALL
                  DEPENDS ${GEOMMATH_LIB_FILE}
                )

