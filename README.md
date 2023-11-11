# cpplox

cpplox is a Lox interpreter written in C++.

The implementation is similar to the `jlox` Java implementation from the [Crafting Interpreters](https://craftinginterpreters.com/) book
with the main implementation difference being the language and the use of `std::variant` instead of the visitor pattern.

The following additional native functions are implemented to allow running [Lox.lox](https://github.com/mrjameshamilton/loxlox), an Lox interpreter written in Lox:

  - `read()` reads a byte from `stdin` or `nil` if end of stream
  - `utf(byte, byte, byte, byte)` converts 1, 2, 3, or 4 bytes into a UTF string
  - `printerr(string)` prints a string to `stderr`
  - `exit(number)` exits with the specific exit code

# Build & run

```shell
$ cmake -G Ninja -B build
$ cd build
$ ninja
$ ./cpplox ../examples/helloworld.lox
```

# Running tests

The interpreter passes the [jlox test suite](https://github.com/munificent/craftinginterpreters/tree/master/test):

```shell
$ dart tool/bin/test.dart -i cpplox jlox
```
