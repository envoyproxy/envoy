#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef SSL3_RT_APPLICATION_DATA \
  --uncomment-macro-redef SSL3_RT_ALERT \
  --uncomment-macro SSL3_RT_MAX_PLAIN_LENGTH \
