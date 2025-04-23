cmake_minimum_required( VERSION 3.5 FATAL_ERROR)

project(SceneTest LANGUAGES CXX)

add_executable(SceneTest WIN32)

set_target_properties(SceneTest PROPERTIES
CXX_STANDARD 20
CXX_EXTENSIONS OFF
CXX_STANDARD_REQUIRED ON
)


target_sources(SceneTest
    PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/Main.cpp
    )

target_include_directories(SceneTest
    PRIVATE
    ${INCLUDE_DIR}
)

target_link_libraries(SceneTest PRIVATE MEngine)

add_custom_command(TARGET SceneTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/Vendor/DirectXShaderCompiler/bin/x64/dxcompiler.dll"
        "$<TARGET_FILE_DIR:SceneTest>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/Vendor/DirectXShaderCompiler/bin/x64/dxil.dll"
        "$<TARGET_FILE_DIR:SceneTest>"
    COMMENT "Copying dxcompiler.dll and dxil.dll to output directory"
)
