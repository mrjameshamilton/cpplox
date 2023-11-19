#include "Compiler.cpp"
#include "Interpreter.cpp"
#include "Parser.cpp"
#include "Resolver.cpp"
#include "Scanner.cpp"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace llvm;
using namespace lox;

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input>"), cl::Required);
cl::opt<std::string> OutputFilename("o", cl::desc("Output LLVM IR file"), cl::value_desc("<output>"));

std::string read_string_from_file(const std::string &file_path) {
    const std::ifstream input_stream(file_path, std::ios_base::binary);

    if (input_stream.fail()) {
        throw std::runtime_error("Failed to open file");
    }

    std::stringstream buffer;
    buffer << input_stream.rdbuf();

    return buffer.str();
}

int main(const int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv);

    if (InputFilename.empty()) {
        std::cout << "source must not be empty";
        return 64;
    }

    Scanner Scanner(read_string_from_file(InputFilename));
    const auto &tokens = Scanner.scanTokens();
    Parser Parser(tokens);
    const auto &ast = Parser.parse();
    if (hadError) return 65;

    Resolver resolver;
    resolver.resolve(ast);
    if (hadError) return 65;


    Compiler Compiler;
    Compiler.evaluate(ast);
    if (!OutputFilename.empty()) {
        Compiler.writeIR(OutputFilename.getValue());
    }

    Interpreter Interpreter;
    Interpreter.evaluate(ast);

    if (hadRuntimeError) return 70;

    return 0;
}
