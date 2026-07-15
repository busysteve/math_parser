cmake_minimum_required(VERSION 3.16)
project(dynamic_formula_engine LANGUAGES CXX)

add_executable(formula_example example.cpp)
target_compile_features(formula_example PRIVATE cxx_std_20)

add_executable(formula_tests tests.cpp)
target_compile_features(formula_tests PRIVATE cxx_std_20)

foreach(target formula_example formula_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

enable_testing()
add_test(NAME formula_tests COMMAND formula_tests)
