import os, sys

apps = ["uname", "calc", "cat", "sh"]
out = "/* Auto-generated embedded userland ELF binaries */\n#ifndef EMBEDDED_APPS_H\n#define EMBEDDED_APPS_H\n#include \"types.h\"\n\n"

for app in apps:
    p = f"bin/{app}"
    if os.path.exists(p):
        with open(p, "rb") as f:
            data = f.read()
        out += f"static const u32 BIN_{app.upper()}_LEN = {len(data)}u;\n"
        out += f"static const u8 BIN_{app.upper()}_DATA[{len(data)}] = {{\n"
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            out += "    " + ", ".join(f"0x{b:02X}u" for b in chunk) + ",\n"
        out += "};\n\n"

out += "#endif\n"
with open("include/embedded_apps.h", "w") as f:
    f.write(out)
print("Generated include/embedded_apps.h successfully.")
