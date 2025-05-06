cmake_minimum_required(VERSION 3.10)

project(DeferredRenderring LANGUAGES CXX)

set(TARGET_NAME DeferredRenderring)
add_executable(${TARGET_NAME} WIN32)

set_target_properties(${TARGET_NAME} PROPERTIES
CXX_STANDARD 20
CXX_EXTENSIONS OFF
CXX_STANDARD_REQUIRED ON
)


target_sources(${TARGET_NAME}
    PRIVATE
    ${PROJECT_SOURCE_DIR}/Source/Main.cpp
    )

target_include_directories(${TARGET_NAME}
    PRIVATE
    ${INCLUDE_DIR}
)

target_link_libraries(${TARGET_NAME} PRIVATE MEngine)

# link asset folder to the build folder
add_custom_command(
    TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_CURRENT_SOURCE_DIR}/Asset"
        "${CMAKE_CURRENT_BINARY_DIR}/Asset"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_CURRENT_SOURCE_DIR}/Shader"
        "${CMAKE_CURRENT_BINARY_DIR}/Shader"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)