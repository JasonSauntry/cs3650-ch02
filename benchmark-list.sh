#!/usr/bin/env bash

SIZE=$1

echo ""
echo "Benchmarking list:"

echo ""
echo "System"
time ./collatz-list-sys $SIZE

echo ""
echo "Hwx"
# time ./collatz-list-hwx $SIZE

echo
echo "Optimized"
# time ./collatz-list-opt $SIZE
