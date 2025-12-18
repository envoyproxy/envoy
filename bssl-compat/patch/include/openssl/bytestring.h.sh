#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-struct cbs_st \
  --uncomment-func-impl CBS_init \
  --uncomment-func-decl CBS_skip \
  --uncomment-func-impl CBS_data \
  --uncomment-func-impl CBS_len \
  --uncomment-func-decl CBS_get_u8 \
  --uncomment-func-decl CBS_get_u16 \
  --uncomment-func-decl CBS_get_u8_length_prefixed \
  --uncomment-func-decl CBS_get_u16_length_prefixed \
  --uncomment-macro CBS_ASN1_TAG_SHIFT \
  --uncomment-macro CBS_ASN1_CONSTRUCTED \
  --uncomment-macro CBS_ASN1_CONTEXT_SPECIFIC \
  --uncomment-macro CBS_ASN1_TAG_NUMBER_MASK \
  --uncomment-macro CBS_ASN1_INTEGER \
  --uncomment-macro CBS_ASN1_OCTETSTRING \
  --uncomment-macro CBS_ASN1_OBJECT \
  --uncomment-macro CBS_ASN1_ENUMERATED \
  --uncomment-macro CBS_ASN1_SEQUENCE \
  --uncomment-macro CBS_ASN1_GENERALIZEDTIME \
  --uncomment-func-decl CBS_get_asn1 \
  --uncomment-func-decl CBS_peek_asn1_tag \
  --uncomment-func-decl CBS_get_any_asn1_element \
  --uncomment-func-decl CBS_get_optional_asn1 \
  --uncomment-func-decl CBS_asn1_oid_to_text \
  --uncomment-struct cbb_buffer_st \
  --uncomment-struct cbb_child_st \
  --uncomment-struct cbb_st \
  --uncomment-func-decl CBB_zero \
  --uncomment-func-decl CBB_init \
  --uncomment-func-decl CBB_cleanup \
  --uncomment-func-decl CBB_finish \
  --uncomment-func-decl CBB_flush \
  --uncomment-func-decl CBB_data \
  --uncomment-func-decl CBB_len \
  --uncomment-func-decl CBB_add_asn1 \
  --uncomment-func-decl CBB_add_bytes \
  --uncomment-func-decl CBB_add_space \
  --uncomment-func-decl CBB_add_u8 \
  --uncomment-func-decl CBB_add_u16 \
  --uncomment-using ScopedCBB \
  --uncomment-func-decl CBS_get_u64_decimal \
  --uncomment-macro CBS_ASN1_BOOLEAN \
  --uncomment-func-decl CBS_get_asn1_bool \
  --uncomment-func-decl CBB_add_asn1_oid_from_text \
  --uncomment-func-decl CBB_add_asn1_uint64 \
  --uncomment-func-decl CBB_add_asn1_uint64_with_tag \
  --uncomment-func-decl CBS_get_bytes \
 