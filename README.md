# cpplox

cpplox is a Lox interpreter written in C++.

The implementation is similar to the `jlox` Java implementation from the [Crafting Interpreters](https://craftinginterpreters.com/) book
with the main implementation difference being the language and the use of `std::variant` instead of the visitor pattern.

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