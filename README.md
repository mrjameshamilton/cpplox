# cpplox

`cpplox` is a Lox interpreter and LLVM compiler written in C++. By default, the `cpplox`
will execute the provide script with the interpreter and if provided an output file, LLVM IR or
an object file will be generated.

## Interpreter

The interpreter implementation is similar to the `jlox` Java implementation from the [Crafting Interpreters](https://craftinginterpreters.com/) book
with the main implementation difference being the language and the use of `std::variant` instead of the visitor pattern.

```shell
$ bin/cpplox examples/helloworld.lox
```

The following additional native functions are implemented to allow running [Lox.lox](https://github.com/mrjameshamilton/loxlox), an Lox interpreter written in Lox:

- `read()` reads a byte from `stdin` or `nil` if end of stream
- `utf(byte, byte, byte, byte)` converts 1, 2, 3, or 4 bytes into a UTF string
- `printerr(string)` prints a string to `stderr`
- `exit(number)` exits with the specific exit code

## LLVM Compiler

The compiler implementation uses LLVM to compile Lox scripts to LLVM IR, 
which can generate textual LLVM IR or object files.

To compile a Lox script, provide a filename with the `-o` command line option.

LLVM IR files can then be executed with the `lli` interpreter:

```shell
$ bin/cpplox examples/helloworld.lox -o helloworld.ll
$ lli helloworld.ll
```

The compiler can also produce an object file which can be linked
into an executable:

```shell
$ bin/cpplox examples/helloworld.lox -o helloworld.o
$ clang helloworld.o -o helloworld
$ ./helloworld
```

### Implementation details

Multiple techniques from the `clox` C implementation from the [Crafting Interpreters](https://craftinginterpreters.com/)
are used in the `cpplox` implementation:

* NaN boxing, with values (numbers, boolean, nil and object pointers) stored as `i64`
* interned strings with a hash table implementation based on the one in `clox`
* upvalues for capturing closed over variables
    - upvalues are closed when the local goes out of scope
* all methods and functions are wrapped in closures for consistency
    - function / method have a runtime representation with their implementations as LLVM IR functions
    - all closures have a receiver parameter and a list of upvalues
* mark & sweep garbage collector

# Build

The build uses cmake and ninja and produces a binary `cpplox` in the `bin` folder:

```shell
$ mkdir build
$ cmake -S . -G Ninja -B build
$ ninja -C build
$ bin/cpplox ../examples/helloworld.lox
```

# Running tests

The interpreter passes the [jlox test suite](https://github.com/munificent/craftinginterpreters/tree/master/test) which
can be checked out and executed via `ninja test`:

```shell
$ mkdir build
$ CRAFTING_INTERPRETERS_PATH=/path/to/craftinginterpreters cmake -S . -B build -G Ninja
$ ninja -C build
$ ninja -C test
```

# Docker

A Dockerfile is provided that contains the required dependencies and can be
used to build `cpplox` and clone & run the Crafting Interpreters test suite:

```shell
$ docker build -t cpploxbuilder .
$ docker run --mount type=bind,source="$(pwd)",target=/app --rm cpploxbuilder
```
