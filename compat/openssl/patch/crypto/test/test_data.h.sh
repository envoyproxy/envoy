#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
--uncomment-regex 'std::string\s*GetTestData\s*(.*);' \
