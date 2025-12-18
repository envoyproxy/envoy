#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl OBJ_txt2obj \
  --uncomment-func-decl OBJ_obj2txt \
  --uncomment-func-decl OBJ_cmp \
  --uncomment-func-decl OBJ_obj2nid \
  --uncomment-macro-redef 'OBJ_R_[[:alnum:]_]*'
  
