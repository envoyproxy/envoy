#!/bin/bash

set -e

./tools/api/clone.sh v2 v3alpha
./tools/check_format.py fix
