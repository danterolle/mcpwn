cmake_minimum_required(VERSION 3.15)
project(mcpwn_executor)

# Set the C++ standard globally for all targets
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(command_executor SHARED
    command_executor.cpp
    command_executor.h
)

# Add target-specific compile options
target_compile_options(command_executor PRIVATE
    -Wall
    -Wextra
    -O3
    -fPIC
)

install(TARGETS command_executor
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(FILES command_executor.h
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
)