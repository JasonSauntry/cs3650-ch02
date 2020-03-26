#!/usr/bin/env bash

for i in `seq 1 $1`;
do
	echo $i:
	./collatz-ivec-opt $i || break
done
