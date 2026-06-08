#!/bin/bash
cd xed
./mfile.py --static --no-encoder --compiler=clang extra_flags='-target x86_64-unknown-windows -nostdlib -ffreestanding -fshort-wchar -mno-red-zone -I../src' --no-api-check --no-amd
