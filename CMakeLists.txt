cmake_minimum_required(VERSION 3.26)
project(cpplox)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER "g++-13")

find_package(LLVM 19 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(cpplox src/main.cpp
        src/frontend/Token.h
        src/frontend/Scanner.h
        src/frontend/Error.h
        src/frontend/AST.h
        src/interpreter/Interpreter.h
        src/Util.h
        src/frontend/Resolver.h
        src/compiler/Expr.cpp
        src/compiler/ModuleCompiler.cpp
        src/compiler/Value.h
        src/compiler/Value.cpp
        src/compiler/String.cpp
        src/compiler/Memory.cpp
        src/compiler/Stmt.cpp
        src/compiler/ModuleCompiler.h
        src/compiler/LoxBuilder.h
        src/compiler/FunctionCompiler.cpp
        src/compiler/FunctionCompiler.h
        src/compiler/Function.cpp
        src/compiler/LoxModule.h
        src/compiler/Upvalue.cpp
        src/compiler/Upvalue.h
        src/compiler/Class.cpp
        src/compiler/Table.cpp
        src/compiler/Callstack.h
        src/frontend/Parser.h
        src/compiler/Callstack.cpp
        src/Debug.h
        src/compiler/GC.cpp
        src/compiler/GC.h
        src/compiler/Table.h
        src/compiler/Memory.h
        src/compiler/Stack.h
        src/compiler/Stack.cpp
)

target_compile_options(cpplox PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
#        -std=c++20 -stdlib=libc++
)

llvm_map_components_to_libnames(llvm_libs support core irreader passes)
target_link_libraries(cpplox ${llvm_libs})
