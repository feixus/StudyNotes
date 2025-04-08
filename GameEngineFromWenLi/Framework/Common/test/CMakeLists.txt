add_executable(AssetLoaderTest AssetLoaderTest.cpp)

target_link_libraries(AssetLoaderTest Common)

add_test(NAME Test_AssetLoader COMMAND AssetLoaderTest)