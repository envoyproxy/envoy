#!/bin/bash

# Copy all BUILD.wip files to BUILD.
for n in `find . -name BUILD.wip`;
do
  cp -f $n $(dirname $n)/BUILD
done
