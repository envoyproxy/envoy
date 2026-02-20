#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '\#include <' \
  --uncomment-func-impl cbs_get \
  --uncomment-func-impl CBS_get_bytes \
  --uncomment-func-impl CBS_skip \
  --uncomment-func-impl cbs_get_u \
  --uncomment-func-impl CBS_get_u8 \
  --uncomment-func-impl CBS_get_u16 \
  --uncomment-func-impl CBS_get_u64_decimal \
  --uncomment-func-impl cbs_get_length_prefixed \
  --uncomment-func-impl CBS_get_u16_length_prefixed \
  --uncomment-func-impl parse_base128_integer \
  --uncomment-func-impl parse_asn1_tag \
  --uncomment-func-impl cbs_get_any_asn1_element \
  --uncomment-static-func-impl cbs_get_asn1 \
  --uncomment-static-func-impl add_decimal \
  --uncomment-func-impl CBS_get_u8_length_prefixed \
  --uncomment-func-impl CBS_get_asn1 \
  --uncomment-func-impl CBS_get_asn1_element \
  --uncomment-func-impl CBS_get_optional_asn1 \
  --uncomment-func-impl CBS_asn1_oid_to_text \
  --uncomment-func-impl CBS_get_any_asn1_element \
  --uncomment-func-impl CBS_peek_asn1_tag \
  --uncomment-func-impl CBS_get_asn1_bool
