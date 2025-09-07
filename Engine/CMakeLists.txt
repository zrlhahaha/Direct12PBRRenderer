cmake_minimum_required(VERSION 3.10)

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/Binary/Release)
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/Binary/Debug)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Source)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Include)
set(TARGET_NAME MEngine)

message(${CMAKE_CXX_COMPILER})

add_library(${TARGET_NAME} STATIC)

set(SOURCE_FILES
    ${SOURCE_DIR}/App.cpp
    ${SOURCE_DIR}/Plateform/Windows/WindowsUtils.cpp
    ${SOURCE_DIR}/Plateform/Windows/Input.cpp
    ${SOURCE_DIR}/Renderer/Device/Direct12/D3D12Device.cpp
    ${SOURCE_DIR}/Renderer/Device/Direct12/D3D12CommandList.cpp
    ${SOURCE_DIR}/Renderer/Device/Direct12/MemoryAllocator.cpp
    ${SOURCE_DIR}/Renderer/Device/Direct12/DeviceResource.cpp
    ${SOURCE_DIR}/Renderer/Device/Direct12/DescriptorAllocator.cpp
    ${SOURCE_DIR}/Renderer/Texture.cpp
    ${SOURCE_DIR}/Renderer/Camera.cpp
    ${SOURCE_DIR}/Renderer/Scene.cpp
    ${SOURCE_DIR}/Renderer/RenderScheduler.cpp
    ${SOURCE_DIR}/Renderer/FrameGraph.cpp
    ${SOURCE_DIR}/Renderer/FrameGraphResource.cpp
    ${SOURCE_DIR}/Renderer/Pipeline/IPipeline.cpp
    ${SOURCE_DIR}/Renderer/Pipeline/DeferredPipeline.cpp
    ${SOURCE_DIR}/Resource/ResourceLoader.cpp
    ${SOURCE_DIR}/Resource/ResourceDef.cpp
    ${SOURCE_DIR}/Resource/DefaultResource.cpp
    ${SOURCE_DIR}/Resource/Shader.cpp
    ${SOURCE_DIR}/Resource/TextureCompression.cpp
    ${SOURCE_DIR}/Resource/BasicStorage.cpp
    ${SOURCE_DIR}/Resource/tiny_obj_loader.cc
    ${SOURCE_DIR}/Utils/Console.cpp
    ${SOURCE_DIR}/Utils/Thread.cpp
    ${SOURCE_DIR}/Utils/Misc.cpp
    ${SOURCE_DIR}/Utils/ConsoleCommand.cpp
    ${SOURCE_DIR}/Utils/SH.cpp
    ${SOURCE_DIR}/Utils/MathLib.cpp
    ${SOURCE_DIR}/Utils/LooseOctree.cpp
)

set(HEADER_FILES
    ${INCLUDE_DIR}/App.h
    ${INCLUDE_DIR}/Fundation.h
    ${INCLUDE_DIR}/Plateform/Windows/WindowsUtils.h
    ${INCLUDE_DIR}/Plateform/Windows/Input.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/D3D12Device.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/D3D12CommandList.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/MemoryAllocator.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/D3DUtils.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/d3dx12.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/DeviceResource.h
    ${INCLUDE_DIR}/Renderer/Device/Direct12/DescriptorAllocator.h
    ${INCLUDE_DIR}/Renderer/Pipeline/IPipeline.h
    ${INCLUDE_DIR}/Renderer/Pipeline/DeferredPipeline.h
    ${INCLUDE_DIR}/Renderer/Texture.h
    ${INCLUDE_DIR}/Renderer/RenderScheduler.h
    ${INCLUDE_DIR}/Renderer/Camera.h
    ${INCLUDE_DIR}/Renderer/Scene.h
    ${INCLUDE_DIR}/Renderer/FrameGraph.h
    ${INCLUDE_DIR}/Renderer/FrameGraphResource.h
    ${INCLUDE_DIR}/Utils/Console.h
    ${INCLUDE_DIR}/Utils/Allocator.h
    ${INCLUDE_DIR}/Utils/Constexpr.h
    ${INCLUDE_DIR}/Utils/Thread.h
    ${INCLUDE_DIR}/Utils/Misc.h
    ${INCLUDE_DIR}/Utils/MathLib.h
    ${INCLUDE_DIR}/Utils/Reflection.h
    ${INCLUDE_DIR}/Utils/ReflectionDef.h
    ${INCLUDE_DIR}/Utils/Serialization.h
    ${INCLUDE_DIR}/Utils/ConsoleCommand.h
    ${INCLUDE_DIR}/Utils/SH.h
    ${INCLUDE_DIR}/Utils/LooseOctree.h
    ${INCLUDE_DIR}/Utils/Time/GameTimer.h
    ${INCLUDE_DIR}/Resource/DefaultResource.h
    ${INCLUDE_DIR}/Resource/ResourceLoader.h
    ${INCLUDE_DIR}/Resource/tiny_obj_loader.h
    ${INCLUDE_DIR}/Resource/ResourceDef.h
    ${INCLUDE_DIR}/Resource/VertexLayout.h
    ${INCLUDE_DIR}/Resource/Shader.h
    ${INCLUDE_DIR}/Resource/json.hpp
    ${INCLUDE_DIR}/Resource/BasicStorage.h
    ${INCLUDE_DIR}/Resource/TextureCompression.h
)

target_sources(${TARGET_NAME}
    PRIVATE
    ${SOURCE_FILES}
    PUBLIC
    ${HEADER_FILES}
)

# generate visual studio folder
source_group(TREE ${SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCE_FILES})
source_group(TREE ${INCLUDE_DIR} PREFIX "Header Files" FILES ${HEADER_FILES})


set_target_properties(${TARGET_NAME} PROPERTIES
CXX_STANDARD 20
CXX_EXTENSIONS OFF
CXX_STANDARD_REQUIRED ON
)

# include vendor directory
target_include_directories(${TARGET_NAME}
# expose the include directory to the project linking this library
    PUBLIC
    ${VENDOR_DIR}/cmdline
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

# link to directxtex, dxc and d3d3 lib files
# since this is a lib file, all dependencies will be specified as PUBLIC
set(D3D12_LIB d3dcompiler D3D12 D3D11 dxguid dxgi ${DXCOMPILER_TARGET})

target_link_libraries(${TARGET_NAME} PUBLIC ${D3D12_LIB})

# enable SSE4.1
target_compile_options(${TARGET_NAME} PUBLIC /arch:AVX)

find_package(directxtex CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Microsoft::DirectXTex)

find_package(directx-dxc CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Microsoft::DirectXShaderCompiler)

find_package(winpixevent CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PUBLIC Microsoft::WinPixEventRuntime)