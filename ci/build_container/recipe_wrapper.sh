#!/bin/bash

PS4='+ $(date "+%s.%N") '
set -x

if [[ `uname` == "Darwin" ]]; then
  function sha256sum {
    gsha256sum
  }
fi

. $1

echo DONE
