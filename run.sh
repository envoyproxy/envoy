#!/bin/bash
for i in {1..250..1}
do
  echo loop ${i}
  dd if=/dev/urandom of=./randfiles/rund${i} bs=1KB count=1000
done
