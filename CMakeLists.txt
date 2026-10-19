cmake_minimum_required(VERSION 3.20)
project(minibox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_executable(minibox src/main.cpp)
target_compile_options(minibox PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)
