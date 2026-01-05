mkdir build
pushd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config debug
popd
pause