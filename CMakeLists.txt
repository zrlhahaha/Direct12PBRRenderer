cmake_minimum_required( VERSION 3.5 FATAL_ERROR)

Set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/Vendor)

project(MEngine LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/Vendor)
add_subdirectory(${CMAKE_SOURCE_DIR}/Engine)
add_subdirectory(${CMAKE_SOURCE_DIR}/SceneTest)
add_subdirectory(${CMAKE_SOURCE_DIR}/UnitTest)

