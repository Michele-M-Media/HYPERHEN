#!/usr/bin/env python3
# Build-time helper: embeds a binary file as a static C byte array.
#
# Usage: gen_blob_header.py <blob_path> <header_out_path> <array_name>

import sys


def main():
    blob_path, header_path, array_name = sys.argv[1:4]
    with open(blob_path, "rb") as f:
        data = f.read()

    with open(header_path, "w") as f:
        f.write("static const unsigned char %s[] = {\n" % array_name)
        for i in range(0, len(data), 20):
            f.write(",".join(str(b) for b in data[i:i + 20]))
            f.write(",\n")
        f.write("};\n")


if __name__ == "__main__":
    main()
