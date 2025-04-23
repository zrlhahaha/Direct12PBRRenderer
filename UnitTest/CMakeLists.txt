cmake_minimum_required( VERSION 3.5 FATAL_ERROR)

project(Test LANGUAGES CXX)


set(GTEST_LIB gtest gtest_main)
set(ENGINE_LIB MEngine)


Set(TestCase
Source/MemoryAllocatorTest.cpp
Source/ThreadPoolTest.cpp
)

add_executable(Test ${TestCase})

target_include_directories(Test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/Vendor/googletest-1.14.0/googletest/include
    ${CMAKE_SOURCE_DIR}/Engine/Include
)

target_link_libraries(Test ${ENGINE_LIB} ${GTEST_LIB})
target_link_directories(Test PRIVATE ${CMAKE_SOURCE_DIR}/Vendor/googletest-1.14.0/lib)

set_target_properties(Test PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON

    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Binary
)

add_test(NAME Test COMMAND Test)
