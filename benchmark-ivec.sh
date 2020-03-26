#!/usr/bin/env bash

SIZE=$1

echo ""
echo "Benchmarking vector:"

echo ""
echo "System:"
time ./collatz-ivec-sys $SIZE

echo ""
echo "Hwx"
# time ./collatz-ivec-hwx $SIZE

echo ""
echo "Optimized"
time ./collatz-ivec-opt $SIZE
