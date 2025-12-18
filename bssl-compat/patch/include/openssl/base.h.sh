#!/bin/bash

set -euo pipefail

MYTMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$MYTMPDIR"' EXIT

cat > "$MYTMPDIR/extraincs" <<EOF

#define BSSL_COMPAT
#include <ossl/openssl/types.h>
#include <ossl/openssl/ssl.h>
#include <ossl/openssl/x509v3.h>
#include <ossl/openssl/sha.h>
EOF

uncomment.sh "$1" --comment -h \
  --sed "/#include\s<openssl\/opensslconf\.h>/ e cat $MYTMPDIR/extraincs" \
  --uncomment-typedef CRYPTO_THREADID \
  --uncomment-typedef CBB \
  --uncomment-typedef CBS \
  --uncomment-typedef CRYPTO_BUFFER_POOL \
  --uncomment-typedef CRYPTO_BUFFER \
  --uncomment-typedef SSL_CLIENT_HELLO \
  --uncomment-typedef SSL_PRIVATE_KEY_METHOD \
  --uncomment-typedef ossl_ssize_t \
  --uncomment-typedef CBS_ASN1_TAG \
  --uncomment-typedef-redef ASN1_TYPE \
  --uncomment-typedef-redef ASN1_BIT_STRING \
  --uncomment-typedef-redef ASN1_PRINTABLESTRING \
  --uncomment-typedef-redef ASN1_T61STRING \
  --uncomment-typedef-redef ASN1_GENERALSTRING \
  --uncomment-typedef-redef ASN1_UTCTIME \
  --uncomment-typedef-redef ASN1_GENERALIZEDTIME \
  --uncomment-typedef-redef ASN1_VISIBLESTRING \
  --uncomment-typedef-redef ASN1_UTF8STRING \
  --uncomment-typedef-redef ASN1_TIME \
  --uncomment-typedef-redef ASN1_ITEM \
  --uncomment-typedef-redef ASN1_OBJECT \
  --uncomment-typedef-redef ASN1_BMPSTRING \
  --uncomment-typedef-redef ASN1_IA5STRING \
  --uncomment-typedef-redef ASN1_UNIVERSALSTRING \
  --uncomment-typedef-redef ASN1_INTEGER \
  --uncomment-typedef-redef ASN1_OCTET_STRING \
  --uncomment-typedef-redef ASN1_STRING \
  --uncomment-typedef-redef ASN1_ENUMERATED \
  --uncomment-typedef-redef BASIC_CONSTRAINTS \
  --uncomment-typedef-redef NAME_CONSTRAINTS \
  --uncomment-typedef-redef OPENSSL_INIT_SETTINGS \
  --uncomment-typedef-redef X509_VERIFY_PARAM \
  --uncomment-typedef-redef X509_CRL \
  --uncomment-typedef-redef X509_EXTENSION \
  --uncomment-typedef-redef X509_INFO \
  --uncomment-typedef-redef X509_NAME_ENTRY \
  --uncomment-typedef-redef X509_NAME \
  --uncomment-typedef-redef X509_PUBKEY \
  --uncomment-typedef-redef BIGNUM \
  --uncomment-typedef-redef BUF_MEM \
  --uncomment-typedef-redef BIO \
  --uncomment-typedef-redef BN_CTX \
  --uncomment-typedef-redef BN_GENCB \
  --uncomment-typedef-redef EC_GROUP \
  --uncomment-typedef-redef EC_KEY \
  --uncomment-typedef-redef EC_POINT \
  --uncomment-typedef-redef ENGINE \
  --uncomment-typedef-redef EVP_MD_CTX --sed 's/ossl_env_md_ctx_st/ossl_evp_md_ctx_st/' \
  --uncomment-typedef-redef EVP_MD --sed 's/ossl_env_md_st/ossl_evp_md_st/' \
  --uncomment-typedef-redef EVP_CIPHER_CTX \
  --uncomment-typedef-redef EVP_CIPHER \
  --uncomment-typedef-redef EVP_PKEY_CTX \
  --uncomment-typedef-redef EVP_PKEY \
  --uncomment-typedef-redef HMAC_CTX \
  --uncomment-typedef-redef RSA \
  --uncomment-typedef-redef PKCS12 --sed 's/ossl_pkcs12_st/ossl_PKCS12_st/' \
  --uncomment-typedef-redef SHA256_CTX --sed 's/struct ossl_sha256_state_st/struct ossl_SHA256state_st/' \
  --uncomment-typedef-redef SSL_CIPHER \
  --uncomment-typedef-redef SSL_CTX \
  --uncomment-typedef-redef SSL_METHOD \
  --uncomment-typedef-redef SSL_SESSION \
  --uncomment-typedef-redef SSL \
  --uncomment-typedef-redef X509 \
  --uncomment-typedef-redef X509_REVOKED \
  --uncomment-typedef-redef X509_STORE \
  --uncomment-typedef-redef ECDSA_SIG --sed 's/ossl_ecdsa_sig_st/ossl_ECDSA_SIG_st/' \
  --uncomment-typedef-redef BIO_METHOD \
  --uncomment-macro BORINGSSL_API_VERSION \
  --uncomment-macro OPENSSL_EXPORT \
  --uncomment-macro OPENSSL_MSVC_PRAGMA \
  --uncomment-macro OPENSSL_UNUSED \
  --uncomment-macro OPENSSL_INLINE \
  --uncomment-macro BORINGSSL_ENUM_INT \
  --uncomment-macro BORINGSSL_NO_CXX \
  --uncomment-macro 'OPENSSL_\(THREADS\|IS_BORINGSSL\|VERSION_NUMBER\|\)' \
  --uncomment-regex 'enum\s*.*\s*BORINGSSL_ENUM_INT' \
  --uncomment-regex 'namespace\s*internal\s*{' \
  --uncomment-regex '}\s*//\s*namespace\s*internal' \
  --uncomment-macro BORINGSSL_MAKE_DELETER \
  --uncomment-macro BORINGSSL_MAKE_UP_REF \
  --uncomment-macro OPENSSL_PRINTF_FORMAT_FUNC \
  --uncomment-macro BSSL_CHECK \
  --uncomment-macro BSSL_NAMESPACE_BEGIN \
  --uncomment-macro BSSL_NAMESPACE_END \
  --uncomment-regex-range 'template\s*<typename\s*T,\s*typename\s*Enable\s*=\s*void>' 'struct\s*DeleterImpl\s*{};' \
  --uncomment-struct Deleter \
  --uncomment-regex-range 'template\s*<typename\s*T,\s*typename\s*CleanupRet,\s*void\s*(\*init)(T\s*\*),' '};$' \
  --uncomment-regex 'template\s*<typename' 'using\s*UniquePtr' \
  --uncomment-macro OPENSSL_DEPRECATED \
  --uncomment-typedef-redef X509_STORE_CTX \
  --uncomment-typedef-redef GENERAL_NAME \
  --uncomment-typedef-redef X509_ALGOR
