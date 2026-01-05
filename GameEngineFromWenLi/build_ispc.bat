@echo off
::git submodule update --init External/src/ispc
rmdir /s /q External\build\ispc
mkdir External\build\ispc
pushd External\build\ispc

cmake -DCMAKE_INSTALL_PREFIX=../.. -G "Visual Studio 17 2022" ../../src/ispc
cmake --build . --config release --target install

popd

pause

