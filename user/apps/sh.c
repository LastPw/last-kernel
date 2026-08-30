#include "libsys.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    println("==================================================");
    println("  LastOS Userspace Shell (Ring-3 sh v1.0)         ");
    println("  Running in isolated userspace with Syscalls     ");
    println("==================================================");
    
    char line[128];
    while (1) {
        print("user@lastos:~$ ");
        int idx = 0;
        while (idx < 127) {
            char c;
            i32 n = read(0, &c, 1);
            if (n <= 0) { yield(); continue; }
            if (c == '\r' || c == '\n') {
                print("\n");
                break;
            } else if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    print("\b \b");
                }
            } else {
                line[idx++] = c;
                char echo[2] = { c, 0 };
                print(echo);
            }
        }
        line[idx] = '\0';

        if (strcmp(line, "exit") == 0) {
            println("Exiting userspace shell.");
            break;
        } else if (strcmp(line, "help") == 0) {
            println("Builtin commands: help, uname, calc, exit");
        } else if (strcmp(line, "uname") == 0) {
            println("LastOS x86_64 Userspace Ring-3");
        } else if (strcmp(line, "calc") == 0) {
            print("2 + 2 = ");
            print_dec(4);
            print("\n");
        } else if (idx > 0) {
            print("sh: command not found: ");
            println(line);
        }
    }
    return 0;
}
