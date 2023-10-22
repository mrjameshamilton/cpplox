#include "AST.h"
#include "Interpreter.cpp"
#include "Parser.cpp"
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
cl::opt<bool> Execute("e", cl::desc("Execute the brainf*ck program using the interpreter"));
cl::opt<bool> Print("p", cl::desc("Print the brainf*ck source"));
cl::opt<bool> DontOptimize("dontoptimize", cl::desc("Don't optimize the LLVM IR"));

std::string read_string_from_file(const std::string &file_path) {
    const std::ifstream input_stream(file_path, std::ios_base::binary);

    if (input_stream.fail()) {
        throw std::runtime_error("Failed to open file");
    }

    std::stringstream buffer;
    buffer << input_stream.rdbuf();

    return buffer.str();
}

int main(int argc, char **argv) {
    cl::ParseCommandLineOptions(argc, argv);

    if (InputFilename.empty()) {
        std::cout << "source must not be empty";
        return -1;
    }

    Scanner Scanner(read_string_from_file(InputFilename));
    auto tokens = Scanner.scanTokens();

    for (auto token: tokens) {
        std::cout << token.getLexeme() << "\n";
    }

    Parser Parser(tokens);
    Program ast = Parser.parse();
    Interpreter Interpreter{};
    Interpreter.evaluate(ast);

    return 0;
}
