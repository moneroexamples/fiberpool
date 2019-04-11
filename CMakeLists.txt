cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME
        fiberexample)

project(${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)


find_package(Boost REQUIRED
    COMPONENTS fiber)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE 
        examples/main.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        Boost::boost
        Boost::fiber
        pthread)

