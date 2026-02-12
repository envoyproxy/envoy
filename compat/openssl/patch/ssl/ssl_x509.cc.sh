#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --comment-regex '^#include.*internal' \
  --uncomment-func-impl SSL_alert_from_verify_result
