cmake_minimum_required(VERSION 3.26)
project(cpplox)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER "g++-13")

find_package(LLVM 18 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(cpplox src/main.cpp
        src/Token.h
        src/Scanner.cpp
        src/Error.h
        src/AST.h
        src/Parser.cpp
        src/Interpreter.cpp
        src/Util.h
        src/Resolver.cpp
        src/compiler/Compiler.cpp
        src/compiler/Compiler.h
        src/compiler/Value.h
        src/compiler/Value.cpp
        src/compiler/String.cpp
        src/compiler/Truthy.cpp
        src/compiler/Memory.cpp
)

target_compile_options(cpplox PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
#        -std=c++20 -stdlib=libc++
)

llvm_map_components_to_libnames(llvm_libs support core irreader passes)
target_link_libraries(cpplox ${llvm_libs})
