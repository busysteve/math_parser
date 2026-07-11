cmake_minimum_required(VERSION 3.20)
project(math_formula_example LANGUAGES CXX)

add_executable(math_formula_example example.cpp)
target_compile_features(math_formula_example PRIVATE cxx_std_20)
target_compile_options(math_formula_example PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
