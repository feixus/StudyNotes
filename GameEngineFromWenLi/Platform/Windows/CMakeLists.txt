add_executable(WindowsApplication WindowsApplication.cpp)

target_compile_definitions(WindowsApplication PRIVATE _WIN32)

target_link_libraries(WindowsApplication Common)