cmake_minimum_required(VERSION 3.26)
project(lox_llvm)

set(CMAKE_CXX_STANDARD 23)
#set(CMAKE_CXX_COMPILER "g++")

find_package(LLVM 18 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(lox_llvm src/main.cpp
        src/Token.h
        src/Scanner.cpp
        src/Error.h
        src/AST.h
        src/Parser.cpp
        src/Interpreter.cpp
        src/Util.h
)

target_compile_options(lox_llvm PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
#        -std=c++20 -stdlib=libc++
)

llvm_map_components_to_libnames(llvm_libs support core irreader passes)
target_link_libraries(lox_llvm ${llvm_libs})
