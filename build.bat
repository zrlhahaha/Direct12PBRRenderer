chcp 65001
mkdir build
cd build
cmake ../ -G "Visual Studio 17 2022"  -DCMAKE_GENERATOR_TOOLSET=ClangCL
pause