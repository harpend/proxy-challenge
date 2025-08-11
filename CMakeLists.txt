cmake_minimum_required(VERSION 3.10)
project(TcpProxy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -pthread -O3 -static -march=native)

set(SOURCES
    proxy.cpp
    main.cpp
)

add_executable(proxy ${SOURCES})

find_package(Threads REQUIRED)
target_link_libraries(proxy PRIVATE Threads::Threads)