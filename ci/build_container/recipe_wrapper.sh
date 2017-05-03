#!/bin/bash

PS4='+ $(date "+%s.%N") '
set -x

. $1

echo DONE
