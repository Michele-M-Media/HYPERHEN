#!/usr/bin/env bash

cd libtomcrypt
for i in clean -j$(nproc); do make CC='gcc -nostdlib -nostdinc -isystem /proc/'$$'/cwd/../../freebsd-headers  -isystem '"'${x86intrin_path//"'"/"'"'"'"'"'"'"'"}'"' -O3 -march=znver2 -g -ffreestanding -ffunction-sections -fdata-sections -fPIE -fPIC -fvisibility=hidden -include /proc/'$$'/cwd/../overrides.h' $i; done
