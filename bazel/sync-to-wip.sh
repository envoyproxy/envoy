#!/bin/bash

# Copy all BUILD files to BUILD.wip and stage in git index.
for n in `find . -name BUILD`;
do
  cp -f $n $n.wip
  git add $n.wip
done
