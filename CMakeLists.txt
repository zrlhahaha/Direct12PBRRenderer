cmake_minimum_required(VERSION 3.10)


Set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/Vendor)
Set(PROJECT_NAME Renderer)

project(${PROJECT_NAME} LANGUAGES CXX)


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/Engine)
add_subdirectory(${CMAKE_SOURCE_DIR}/DeferredRendering)
add_subdirectory(${CMAKE_SOURCE_DIR}/UnitTest)

