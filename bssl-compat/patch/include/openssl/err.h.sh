#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --sed '/base\.h/a#include <ossl/openssl/err.h>' \
  --sed '/OPENSSL_INLINE int ERR_GET_LIB/i#define ERR_GET_LIB(packed_error) ossl_ERR_GET_LIB(packed_error)' \
  --sed '/OPENSSL_INLINE int ERR_GET_REASON/i#define ERR_GET_REASON(packed_error) ossl_ERR_GET_REASON(packed_error)' \
  --uncomment-func-decl ERR_get_error \
  --uncomment-func-decl ERR_peek_error \
  --uncomment-func-decl ERR_peek_error_line_data \
  --uncomment-func-decl ERR_peek_last_error \
  --uncomment-func-decl ERR_error_string_n \
  --uncomment-func-decl ERR_lib_error_string \
  --uncomment-func-decl ERR_reason_error_string \
  --uncomment-func-decl ERR_print_errors_fp \
  --uncomment-func-decl ERR_clear_error \
  --uncomment-regex 'enum\s{' \
  --sed 's|^// \([ \t]*\)\(ERR_LIB_[a-zA-Z0-9_]*\)[^a-zA-Z0-9_].*$|#ifdef ossl_\2\n\1\2 = ossl_\2,\n#endif|g' \
  --uncomment-regex '\s\sERR_NUM_LIBS' '};' \
  --uncomment-macro 'ERR_R_[A-Z0-9_]*_LIB' \
  --uncomment-macro-redef 'ERR_R_[[:alnum:]_]*' \
  --sed 's|ossl_ERR_R_OVERFLOW|ossl_ERR_R_INTERNAL_ERROR|' \
  --uncomment-func-decl ERR_func_error_string \
  --uncomment-func-decl ERR_error_string \
  --uncomment-macro ERR_ERROR_STRING_BUF_LEN \
  --sed '/OPENSSL_INLINE int ERR_GET_FUNC/i#define ERR_GET_FUNC(packed_error) ossl_ERR_GET_FUNC(packed_error)' \
  --uncomment-macro OPENSSL_PUT_ERROR \
  --uncomment-func-decl ERR_put_error \
  --uncomment-macro-redef ERR_NUM_ERRORS \
  --sed '/#define ERR_PACK/i#define ERR_PACK(lib, reason) (lib == ossl_ERR_LIB_SYS) ? (ossl_ERR_SYSTEM_FLAG | reason) : ossl_ERR_PACK(lib, 0, reason)'
