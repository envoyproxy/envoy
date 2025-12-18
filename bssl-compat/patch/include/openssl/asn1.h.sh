#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-macro-redef 'V_ASN1_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'ASN1_R_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'MBSTRING_[a-zA-Z0-9_]*' \
  --uncomment-macro-redef 'ASN1_STRFLGS_[A-Z0-9_]*' \
  --uncomment-macro 'DECLARE_ASN1_ITEM' \
  --uncomment-func-decl ASN1_STRING_free \
  --uncomment-func-decl ASN1_STRING_to_UTF8 \
  --uncomment-func-decl ASN1_STRING_get0_data \
  --uncomment-func-decl ASN1_STRING_data \
  --uncomment-func-decl ASN1_STRING_length \
  --uncomment-func-decl ASN1_STRING_set \
  --uncomment-func-decl ASN1_BMPSTRING_new \
  --uncomment-func-decl ASN1_IA5STRING_new \
  --uncomment-func-decl ASN1_UNIVERSALSTRING_new \
  --uncomment-func-decl ASN1_IA5STRING_free \
  --uncomment-func-decl ASN1_INTEGER_new \
  --uncomment-func-decl ASN1_INTEGER_free \
  --uncomment-func-decl c2i_ASN1_INTEGER \
  --uncomment-func-decl ASN1_INTEGER_to_BN \
  --uncomment-func-decl ASN1_TIME_new \
  --uncomment-func-decl ASN1_TIME_free \
  --uncomment-func-decl ASN1_TIME_diff \
  --uncomment-func-decl ASN1_TIME_set \
  --uncomment-func-decl ASN1_TIME_adj \
  --uncomment-func-decl ASN1_TYPE_new \
  --uncomment-func-decl ASN1_TYPE_set \
  --uncomment-func-decl ASN1_OBJECT_free \
  --uncomment-func-decl ASN1_ENUMERATED_to_BN \
  --uncomment-func-decl i2d_ASN1_OCTET_STRING \
  --uncomment-macro DECLARE_ASN1_FUNCTIONS \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(ASN1_OBJECT' \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(ASN1_STRING' \
  --uncomment-macro DECLARE_ASN1_ALLOC_FUNCTIONS \
  --uncomment-macro DECLARE_ASN1_FUNCTIONS_name \
  --uncomment-macro DECLARE_ASN1_ENCODE_FUNCTIONS \
  --uncomment-macro DECLARE_ASN1_ENCODE_FUNCTIONS_const \
  --uncomment-macro DECLARE_ASN1_FUNCTIONS_const \
  --uncomment-macro DECLARE_ASN1_ALLOC_FUNCTIONS_name \
  --uncomment-macro ASN1_BOOLEAN_TRUE \
  --uncomment-macro ASN1_BOOLEAN_FALSE \
  
