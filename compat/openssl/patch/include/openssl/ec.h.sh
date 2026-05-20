#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl EC_GROUP_get0_order \
  --uncomment-func-decl EC_GROUP_get_curve_name \
  --uncomment-func-decl EC_GROUP_get_degree \
  --uncomment-func-decl EC_POINT_free \
  --uncomment-func-decl EC_POINT_new \
  --uncomment-func-decl EC_POINT_mul \
  --uncomment-macro-redef 'EC_R_[[:alnum:]_]*'
