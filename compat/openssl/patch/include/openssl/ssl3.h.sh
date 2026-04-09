#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro SSL3_RT_MAX_PLAIN_LENGTH \

