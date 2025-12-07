cmake_minimum_required(VERSION 3.28)
project(VHLog)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_ASIO "Build with ASIO support" OFF)

if(WIN32)
    set (CMAKE_SYSTEM_VERSION 10.0)
    if(USE_ASIO)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/asio.hpp OR 
            EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include/asio.hpp)
            message(STATUS "Found ASIO in third_party directory.")
            include_directories(
                ${PROJECT_SOURCE_DIR}/third_party/asio/include
            )
        else()
            message(WARNING "ASIO submodule not found. Please run git submodule update --init before compiling again.")
        endif()
    endif()
    file(GLOB all_SRCS
            "${PROJECT_SOURCE_DIR}/include/*.h"
            "${PROJECT_SOURCE_DIR}/src/*.cpp"
            "${PROJECT_SOURCE_DIR}/examples/*.cpp"
            )
    add_executable(VHLog ${all_SRCS})
    set_property(TARGET VHLog PROPERTY CXX_STANDARD 20)
    if (USE_ASIO)
        target_compile_definitions(VHLog PRIVATE USE_ASIO)
    endif()
else()
    if(USE_ASIO)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/asio.hpp OR 
            EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include/asio.hpp)
            message(STATUS "Found ASIO in third_party directory.")
            include_directories(
                ${PROJECT_SOURCE_DIR}/third_party/asio/include
            )
        else()
            message(WARNING "ASIO submodule not found. Please run git submodule update --init before compiling again.")
        endif()
    endif()
    file(GLOB all_SRCS
            "${PROJECT_SOURCE_DIR}/include/*.h"
            "${PROJECT_SOURCE_DIR}/src/*.cpp"
            "${PROJECT_SOURCE_DIR}/examples/*.cpp"
    )

    add_executable(VHLog ${all_SRCS})
    set_property(TARGET VHLog PROPERTY CXX_STANDARD 20)
    if (USE_ASIO)
        target_compile_definitions(VHLog PRIVATE USE_ASIO)
    endif()
endif()

