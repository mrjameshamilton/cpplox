# cpplox

`cpplox` is a Lox interpreter and LLVM compiler written in C++. By default, the `cpplox`
will execute the provide script with the interpreter and if provided an output file, LLVM IR or
an object file will be generated.

## LLVM Compiler

The [compiler](https://github.com/mrjameshamilton/cpplox/tree/master/src/compiler) uses LLVM to compile Lox scripts to LLVM IR, 
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

* NaN boxing with values (numbers, boolean, nil and object pointers) stored as `i64`
* interned strings using a hash table
* [upvalues](https://craftinginterpreters.com/closures.html#upvalues) for capturing closed over variables
    - upvalues are closed when the local goes out of scope
* all functions and methods are wrapped in closures for consistency
    - functions have a runtime representation with their implementations as LLVM IR functions
    - all closures have a receiver parameter and upvalue parameter
* mark & sweep garbage collector
    - a shadow stack is used to track locals as GC roots
    - temporary locals are inserted when necessary to ensure they are reachable before assignment

## Interpreter

The interpreter implementation is similar to the `jlox` Java implementation from the [Crafting Interpreters](https://craftinginterpreters.com/) book
with the main implementation difference being the language and the use of `std::variant` instead of the visitor pattern.

```shell
$ bin/cpplox examples/helloworld.lox
```

The following additional native functions are implemented in the interpreter to allow running [Lox.lox](https://github.com/mrjameshamilton/loxlox), an Lox interpreter written in Lox:

- `read()` reads a byte from `stdin` or `nil` if end of stream
- `utf(byte, byte, byte, byte)` converts 1, 2, 3, or 4 bytes into a UTF string
- `printerr(string)` prints a string to `stderr`
- `exit(number)` exits with the specific exit code

# Build

The build uses cmake and ninja and produces a binary `cpplox` in the `bin` folder:

```shell
$ mkdir build
$ cmake -S . -G Ninja -B build
$ ninja -C build
$ bin/cpplox ../examples/helloworld.lox
```

# Performance

As a quick performance test, running the below fibonacci example,
gives the following run times (on my laptop, approximate average over several runs):

<table>
  <tr>
    <td>LLVM compiler</td>
    <td>clox</td>
  </tr>
  <tr>
    <td>0.15 seconds</td>
    <td>0.55 seconds</td>
  </tr>
</table>

```javascript
fun fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}

var start = clock();
print fib(40);
var end = clock();
print end - start;
```

# Lox.lox

Both the interpreter and compiler can execute [Lox.lox](https://github.com/mrjameshamilton/loxlox), a working-but-slow
Lox interpreter written in Lox itself:

```shell
$ bin/cpplox Lox.lox -o loxlox.ll
$ clang loxlox.ll -o loxlox
$ cat examples/fib.lox | ./loxlox
832040
38.4879
```

# Running tests

The interpreter passes the [jlox test suite](https://github.com/munificent/craftinginterpreters/tree/master/test) which
can be checked out and executed via `ninja test`:

```shell
$ mkdir build
$ CRAFTING_INTERPRETERS_PATH=/path/to/craftinginterpreters cmake -S . -B build -G Ninja
$ ninja -C build
$ ninja -C build test
```

# Docker

A Dockerfile is provided that contains the required dependencies and can be
used to build `cpplox` and clone & run the Crafting Interpreters test suite:

```shell
$ docker build -t cpploxbuilder .
$ docker run --mount type=bind,source="$(pwd)",target=/app --rm cpploxbuilder
```
