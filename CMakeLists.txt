# cmake_minimum_required(VERSION <specify CMake version here>)
project(.)

set(CMAKE_CXX_STANDARD 17)

add_executable(.
        Source.cpp MemoryManager.h Utility.h MemoryManager.cpp)
