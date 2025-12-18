#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --comment-regex '#include\s*"\.\./x509/internal\.h"'