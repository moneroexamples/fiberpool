cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME
        fibertests)

project(${PROJECT_NAME})

add_executable(${PROJECT_NAME}
        EXCLUDE_FROM_ALL
        fibertests.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        Boost::fiber
        pthread)

add_test(fibertest ${PROJECT_NAME})

# "make check" will compile and run the tests
# from https://stackoverflow.com/a/736838/248823
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
                      DEPENDS ${PROJECT_NAME})

 
