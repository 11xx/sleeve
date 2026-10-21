"""Fails unless a WebAssembly module imports nothing, and says what it exports.

usage: python3 -I tests/wasm-imports.py MODULE.wasm
"""
import sys


def leb(data, at):
    value = shift = 0
    while True:
        b = data[at]
        at += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if b < 0x80:
            return value, at


def main(path):
    data = open(path, "rb").read()
    if data[:8] != b"\0asm\1\0\0\0":
        sys.exit(f"FAIL freestanding: {path} is not a WebAssembly module")
    imports = exports = 0
    at = 8
    while at < len(data):
        section, at = data[at], at + 1
        size, at = leb(data, at)
        if section in (2, 7):
            count, _ = leb(data, at)
            if section == 2:
                imports = count
            else:
                exports = count
        at += size
    if imports:
        print(f"FAIL freestanding: the module imports {imports} names")
        return 1
    print(f"pass freestanding: the whole library for wasm32 without a C library exports "
          f"{exports} names and imports none")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
