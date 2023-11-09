#ifndef LOX_LLVM_UTIL_H
#define LOX_LLVM_UTIL_H
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class Uncopyable {
public:
    Uncopyable(const Uncopyable &) = delete;
    Uncopyable &operator=(const Uncopyable &) = delete;

protected:
    Uncopyable() = default;
    ~Uncopyable() = default;
};

#endif//LOX_LLVM_UTIL_H
