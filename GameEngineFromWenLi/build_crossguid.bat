@echo off
::git submodule update --init External/src/crossguid
rmdir /s /q External\build\crossguid
mkdir External\build\crossguid
pushd External\build\crossguid
cmake -DCMAKE_INSTALL_PREFIX=../.. -G "Visual Studio 17 2022" ../../src/crossguid
::cmake -G "Ninja" ../../src/crossguid -DCMAKE_INSTALL_PREFIX=../.. -DCMAKE_MAKE_PROGRAM="C:/Tools/ninja.exe" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config release --target install
popd

pause

