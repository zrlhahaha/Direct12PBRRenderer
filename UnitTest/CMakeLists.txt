cmake_minimum_required(VERSION 3.10)

Set(TARGET_NAME UnitTest)
set(ENGINE_LIB MEngine)

Set(SOURCES
Source/MemoryAllocatorTest.cpp
Source/ThreadPoolTest.cpp
Source/Main.cpp
)

add_executable(${TARGET_NAME} ${SOURCES})

set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
    CXX_STANDARD_REQUIRED ON

    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Binary
)

# link to engine lib
target_link_libraries(${TARGET_NAME} PRIVATE ${ENGINE_LIB})

# link to gtest
enable_testing()

find_package(GTest CONFIG REQUIRED)
target_link_libraries(${TARGET_NAME} PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)

add_test(Name ${TARGET_NAME} Command ${TARGET_NAME})
