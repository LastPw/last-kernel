#include "libsys.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    println("=== LastOS Userspace Math Calculator ===");
    println("Demonstrating 64-bit ALU operations in Ring-3:");
    
    i64 a = 1048576;
    i64 b = 512;
    print("  1048576 * 512 = ");
    print_dec(a * b);
    print("\n");

    print("  1048576 / 512 = ");
    print_dec(a / b);
    print("\n");

    print("  Fibonacci(12) = ");
    i64 f0 = 0, f1 = 1;
    for (int i = 0; i < 12; i++) {
        i64 next = f0 + f1;
        f0 = f1;
        f1 = next;
    }
    print_dec(f0);
    print("\n");

    return 0;
}
