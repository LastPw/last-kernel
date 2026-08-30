#include "libsys.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        println("Usage: cat <filename>");
        return 1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        print("cat: cannot open file '");
        print(argv[1]);
        println("'");
        return 1;
    }
    char buf[256];
    i32 n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }
    close(fd);
    return 0;
}
