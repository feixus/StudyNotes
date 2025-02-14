# cmake -G "Unix Makefiles" .. and make for clang/clang++ GNU-like command-line in build folder:
#   1.  安装make
#   2. 在make时, set (CMAKE_C_FLAGS_DEBUG "-g") 和 set(CMAKE_CXX_FLAGS_DEBUG "-g") 为何built target Empty报错(windows platform)
        #lld-link: error: <root>: undefined symbol: mainCRTStartup
        #lld-link: error: undefined symbol: void __cdecl operator delete(void *, unsigned __int64)
        #>>> referenced by F:\StudyNotes\Graphics\GameEngineFromWenLi\Framework\Common\BaseApplication.hpp:7
        #>>>               CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(public: virtual void * __cdecl My::BaseApplication::`scalar deleting dtor'(unsigned int))
        #
        #lld-link: error: undefined symbol: const type_info::`vftable'
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(class My::BaseApplication `RTTI Type Descriptor')
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(class My::IApplication `RTTI Type Descriptor')
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(class My::IRuntimeModule `RTTI Type Descriptor')
        #
        #lld-link: error: undefined symbol: _purecall
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(const My::IApplication::`vftable')
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(const My::IApplication::`vftable')
        #>>> referenced by CMakeFiles/Empty.dir/EmptyApplication.cpp.obj:(const My::IApplication::`vftable')
        #>>> referenced 4 more times
        #clang++: error: linker command failed with exit code 1 (use -v to see invocation)


cmake_minimum_required(VERSION 3.1)
set (CMAKE_C_COMPILER                "clang")
set (CMAKE_C_FLAGS                  "-Wall")
#set (CMAKE_C_FLAGS_DEBUG            "-g")
set (CMAKE_C_FLAGS_MINSIZEREL       "-Os -DNDEBUG")
set (CMAKE_C_FLAGS_RELEASE          "-O3 -DNDEBUG")
set (CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 -g")
set (CMAKE_C_STANDARD 11)
set (CMAKE_CXX_COMPILER             "clang++")
set (CMAKE_CXX_FLAGS                "-Wall")
#set(CMAKE_CXX_FLAGS_DEBUG           "-g")
set (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set (CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
project (GameEngineFromWenLi)
include_directories("${PROJECT_SOURCE_DIR}/Framework/Common")
include_directories("${PROJECT_SOURCE_DIR}/Framework/Interface")
include_directories("${PROJECT_SOURCE_DIR}/Platform/Windows")
add_subdirectory(Framework)
add_subdirectory(Platform/Windows)



# cmake -G "Unix Makefiles" .. and make for MSVC-like command line

#cmake_minimum_required(VERSION 3.1)
#set (CMAKE_C_COMPILER               "clang-cl")
#set (CMAKE_C_FLAGS                  "-Wall")
#set (CMAKE_C_FLAGS_DEBUG            "/Debug")
#set (CMAKE_C_FLAGS_MINSIZEREL       "-Os -DNDEBUG")
#set (CMAKE_C_FLAGS_RELEASE          "-O4 -DNDEBUG")
#set (CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 /Debug")
#set (CMAKE_C_STANDARD 11)
#set (CMAKE_CXX_COMPILER             "clang-cl")
#set (CMAKE_CXX_FLAGS                "-Wall -Xclang -std=gnu++14")
#set (CMAKE_CXX_FLAGS_DEBUG          "/Debug")
#set (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
#set (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
#set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 /Debug")
#project (GameEngineFromWenLi)
#include_directories("${PROJECT_SOURCE_DIR}/Framework/Common")
#include_directories("${PROJECT_SOURCE_DIR}/Framework/Interface")
#include_directories("${PROJECT_SOURCE_DIR}/Platform/Windows")
#add_subdirectory(Framework)
#add_subdirectory(Platform/Windows)