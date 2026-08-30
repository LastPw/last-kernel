#include "libsys.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    println("LastKernel (x86_64 Long Mode) v0.2 Monolithic Userspace Ring-3");
    print("Process PID: ");
    print_dec(getpid());
    print("\nSystem Ticks: ");
    print_dec(getticks());
    print("\n");
    return 0;
}
