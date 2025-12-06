cmake_minimum_required(VERSION 3.28)
project(VHLog)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    set (CMAKE_SYSTEM_VERSION 10.0)
    include_directories(
            ${PROJECT_SOURCE_DIR}/third_party/asio/include
    )

    file(GLOB all_SRCS
            "${PROJECT_SOURCE_DIR}/include/*.h"
            "${PROJECT_SOURCE_DIR}/src/*.cpp"
            "${PROJECT_SOURCE_DIR}/examples/*.cpp"
            )

    add_executable(VHLog ${all_SRCS})
    set_property(TARGET VHLog PROPERTY CXX_STANDARD 20)
else()
    include_directories(
            ${PROJECT_SOURCE_DIR}/third_party/asio/include
    )

    file(GLOB all_SRCS
            "${PROJECT_SOURCE_DIR}/include/*.h"
            "${PROJECT_SOURCE_DIR}/src/*.cpp"
            "${PROJECT_SOURCE_DIR}/examples/*.cpp"
    )

    add_executable(VHLog ${all_SRCS})
    set_property(TARGET VHLog PROPERTY CXX_STANDARD 20)
endif()

