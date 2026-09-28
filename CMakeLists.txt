cmake_minimum_required(VERSION 3.12)
project(api_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -pthread)

find_package(Threads REQUIRED)

add_executable(api-analyzer src/main.cpp)
target_link_libraries(api-analyzer PRIVATE Threads::Threads)
