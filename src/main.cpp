#include "compiler/ModuleCompiler.h"
#include "frontend/Parser.h"
#include "frontend/Resolver.h"
#include "frontend/Scanner.h"
#include "interpreter/Interpreter.h"

#include "llvm/Support/CommandLine.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>


using namespace llvm;
using namespace lox;

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input>"), cl::Required);
cl::opt<std::string> OutputFilename("o", cl::desc("Output LLVM IR file"), cl::value_desc("<output>"));
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

    if (!OutputFilename.empty()) {
        const ModuleCompiler ModuleCompiler;
        ModuleCompiler.evaluate(ast);

        if (!ModuleCompiler.initializeTarget()) {
            std::cout << "Could not initialize target machine." << std::endl;
            return 65;
        }

        if (!DontOptimize.getValue()) {
            if (!ModuleCompiler.optimize()) {
                std::cout << "Could not optimize." << std::endl;
                return 65;
            }
        }
        const auto filename = OutputFilename.getValue();
        if (filename.ends_with(".o")) {
            ModuleCompiler.writeObject(filename);
        } else if (filename.ends_with(".ll")) {
            ModuleCompiler.writeIR(filename);
        } else {
            std::cout << "Output file should have .ll or .o extension." << std::endl;
            return 65;
        }
    } else {
        Interpreter Interpreter;
        Interpreter.evaluate(ast);

        if (hadRuntimeError) return 70;
    }

    return 0;
}
