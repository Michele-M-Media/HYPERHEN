#!/usr/bin/env python3
# Build-time helper: extracts a dashboard .inc asset's exact byte content by
# compiling and running it through the host C compiler (so C string-escape
# parsing is never reimplemented by hand), gzip-compresses it, and writes a
# header with the compressed bytes as a static C array.
#
# Usage: gen_gzip_header.py <inc_path> <header_out_path> <array_name>

import gzip
import os
import subprocess
import sys
import tempfile


def extract_raw_bytes(inc_path):
    inc_dir = os.path.dirname(os.path.abspath(inc_path))
    inc_name = os.path.basename(inc_path)
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "dump.c")
        exe = os.path.join(td, "dump")
        with open(src, "w") as f:
            f.write('#include <stdio.h>\n')
            f.write('static const char DATA[] =\n#include "%s"\n;\n' % inc_name)
            f.write('int main(void) { fwrite(DATA, 1, sizeof(DATA) - 1, stdout); return 0; }\n')
        subprocess.run(["cc", "-I", inc_dir, src, "-o", exe], check=True)
        return subprocess.run([exe], check=True, stdout=subprocess.PIPE).stdout


def main():
    inc_path, header_path, array_name = sys.argv[1:4]
    raw = extract_raw_bytes(inc_path)
    gz = gzip.compress(raw, compresslevel=9, mtime=0)

    with open(header_path, "w") as f:
        f.write("static const unsigned char %s[] = {\n" % array_name)
        for i in range(0, len(gz), 20):
            f.write(",".join(str(b) for b in gz[i:i + 20]))
            f.write(",\n")
        f.write("};\n")
        f.write("static const unsigned long %s_len = %dul;\n" % (array_name, len(gz)))
        f.write("static const unsigned long %s_raw_len = %dul;\n" % (array_name, len(raw)))


if __name__ == "__main__":
    main()
