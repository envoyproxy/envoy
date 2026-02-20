#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --uncomment-func-impl CBB_zero \
  --uncomment-func-impl cbb_init \
  --uncomment-func-impl CBB_init \
  --uncomment-func-impl CBB_cleanup \
  --uncomment-static-func-impl cbb_buffer_reserve \
  --uncomment-static-func-impl cbb_buffer_add \
  --uncomment-func-impl CBB_finish \
  --uncomment-func-impl cbb_get_base \
  --uncomment-func-impl CBB_flush \
  --uncomment-func-impl CBB_data \
  --uncomment-func-impl CBB_len \
  --uncomment-static-func-impl cbb_add_child \
  --uncomment-func-impl add_base128_integer \
  --uncomment-func-impl CBB_add_asn1 \
  --uncomment-func-impl CBB_add_bytes \
  --uncomment-func-impl CBB_add_space \
  --uncomment-func-impl cbb_add_u \
  --uncomment-func-impl CBB_add_u8 \
  --uncomment-func-impl CBB_add_u16 \
  --uncomment-func-impl CBB_add_asn1_uint64 \
  --uncomment-func-impl CBB_add_asn1_uint64_with_tag \
  --uncomment-func-impl parse_dotted_decimal \
  --uncomment-func-impl CBB_add_asn1_oid_from_text \
  --uncomment-func-impl cbb_on_error
