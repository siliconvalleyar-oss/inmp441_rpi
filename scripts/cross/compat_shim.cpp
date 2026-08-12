// compat_shim.cpp
//
// Cross-compilation compatibility shim (used only by scripts/cross_build.sh).
//
// The x86_64 cross toolchain (GCC 13) compiles code that may reference
// std::__throw_bad_array_new_length(), which was added in GCC 11. The
// Raspberry Pi 32-bit (bullseye) ships libstdc++ 6.0.28 (GCC 10), which does
// NOT export that symbol, so a cross-linked binary would fail to load there.
//
// This file provides the missing definition. It is only compiled into the
// binary for cross builds; native builds on the Pi use GCC 10 and never
// reference the symbol.
#include <new>

namespace std {
[[noreturn]] void __throw_bad_array_new_length() {
    throw bad_array_new_length();
}
} // namespace std
