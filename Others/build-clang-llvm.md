 
 

# 下载资源 llvm-project
git clone --config core.autocrlf=false https://github.com/llvm/llvm-project.git
  
git clone --config core.autocrlf=false https://mirrors.tuna.tsinghua.edu.cn/git/llvm-project.git  
  
// 签出 如 llvm 19.1.6 release version  
git checkout -b llvm-19.1.6 llvmorg-19.1.6  
  
  
# 构建llvm.sln
cmake -DCMAKE_CXX_FLAGS="/utf-8" -DLLVM_ENABLE_PROJECTS=clang -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release  -A x64 -Thost=x64 ..\llvm    
  
# clang and llvm : release x64
msbuild ALL_BUILD.vcxproj /V:m /p:Platform=x64 /p:Configuration=Release /t:rebuild  
  
  
# 遇到的编译问题

### 问题1
H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\UnicodeCharSets.h(397,1): error C2226: syntax error: unexpected type 'llvm::sys::UnicodeCharRange' [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  
H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\UnicodeCharSets.h(397,69): error C2143: syntax error: missing ';' before '{' [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  
H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\UnicodeCharSets.h(397,69): error C2447: '{': missing function header (old-style formal list?) [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  

H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\Lexer.cpp(1549,59): error C2665: 'llvm::sys::UnicodeCharSet::UnicodeCharSet': no overloaded function could convert all the argument types [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  
H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\Lexer.cpp(1578,9): error C2065: 'C11AllowedIDCharRanges': undeclared identifier [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  
H:\llvm-project-llvmorg-19.1.6\llvm-project\clang\lib\Lex\Lexer.cpp(1618,59): error C2665: 'llvm::sys::UnicodeCharSet::UnicodeCharSet': no overloaded function could convert all the argument types [H:\llvm-project-llvmorg-19.1.6\build\tools\clang\lib\Lex\clangLex.vcxproj]  
  
#### 解决方法
https://github.com/llvm/llvm-project/issues/60549  
-DCMAKE_CXX_FLAGS="/utf-8"   确保MSVC将源码文件视为UTF-8编码  


### 问题2 缺少组件
适用于最新v143生成工具的C++ ATL(x86 和 x64)



