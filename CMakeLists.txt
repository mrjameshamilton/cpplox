cmake_minimum_required(VERSION 3.26)
project(cpplox)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_COMPILER "g++-13")

find_package(LLVM 18.1.3 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS} SYSTEM)
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

get_filename_component(binFile "../bin" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${binFile})

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
        -O3
#        -std=c++20 -stdlib=libc++
)

llvm_map_components_to_libnames(llvm_libs
        ${LLVM_TARGETS_TO_BUILD}
        orcjit
        support
        core
        irreader
        codegen
        mc
        mcparser
        option)
target_link_libraries(cpplox ${llvm_libs})

#set(DART_PATH "/opt/dart-sdk-v2/bin/dart")
#set(CRAFTING_INTERPRETERS_PATH "~/Projects/craftinginterpreters")

set(CRAFTING_INTERPRETERS_PATH $ENV{CRAFTING_INTERPRETERS_PATH})

file(WRITE ${CMAKE_BINARY_DIR}/cpplox-compiler.sh "\
#!/bin/bash\n \
TMPFILE=$(mktemp --suffix .ll)\n \
script_dir=$(dirname \"$0\")\n \
${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpplox  $1 -o $TMPFILE && clang $TMPFILE -o out && ./out \n"
)
file(CHMOD ${CMAKE_BINARY_DIR}/cpplox-compiler.sh PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_WRITE GROUP_EXECUTE WORLD_READ WORLD_WRITE WORLD_EXECUTE)

enable_testing()
add_test(NAME interpreter COMMAND dart tool/bin/test.dart jlox -i ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpplox WORKING_DIRECTORY ${CRAFTING_INTERPRETERS_PATH})
add_test(NAME compiler COMMAND dart tool/bin/test.dart jlox -i ${CMAKE_BINARY_DIR}/cpplox-compiler.sh WORKING_DIRECTORY ${CRAFTING_INTERPRETERS_PATH})
