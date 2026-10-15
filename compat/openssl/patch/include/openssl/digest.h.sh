#!/bin/bash

set -euo pipefail

MYTMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$MYTMPDIR"' EXIT

cat > "$MYTMPDIR/ScopedEVP_MD_CTX.h" <<EOF
class ScopedEVP_MD_CTX {
  public:
    ScopedEVP_MD_CTX() : ctx_(EVP_MD_CTX_new()) {}
    ~ScopedEVP_MD_CTX() { EVP_MD_CTX_free(ctx_); }

    ScopedEVP_MD_CTX(ScopedEVP_MD_CTX &&other) {
      EVP_MD_CTX_move(ctx_, other.ctx_);
    }
    ScopedEVP_MD_CTX &operator=(ScopedEVP_MD_CTX &&other) {
      EVP_MD_CTX_move(ctx_, other.ctx_);
      return *this;
    }

    EVP_MD_CTX *get() { return ctx_; }
    const EVP_MD_CTX *get() const { return ctx_; }

    EVP_MD_CTX *operator->() { return ctx_; }
    const EVP_MD_CTX *operator->() const { return ctx_; }

    void Reset() {
      EVP_MD_CTX_reset(ctx_);
    }
  private:
    EVP_MD_CTX *ctx_;
};
EOF

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl EVP_md4 \
  --uncomment-func-decl EVP_md5 \
  --uncomment-func-decl EVP_sha1 \
  --uncomment-func-decl EVP_sha224 \
  --uncomment-func-decl EVP_sha256 \
  --uncomment-func-decl EVP_sha384 \
  --uncomment-func-decl EVP_sha512 \
  --uncomment-func-decl EVP_sha512_256 \
  --uncomment-func-decl EVP_md5_sha1 \
  --uncomment-func-decl EVP_MD_CTX_new \
  --uncomment-func-decl EVP_MD_CTX_free \
  --uncomment-func-decl EVP_MD_CTX_copy_ex \
  --uncomment-func-decl EVP_MD_CTX_move \
  --uncomment-func-decl EVP_MD_CTX_reset \
  --uncomment-func-decl EVP_MD_CTX_destroy \
  --uncomment-func-decl EVP_DigestInit_ex \
  --uncomment-func-decl EVP_DigestInit \
  --uncomment-func-decl EVP_DigestUpdate \
  --uncomment-macro-redef EVP_MAX_MD_SIZE \
  --uncomment-func-decl EVP_DigestFinal_ex \
  --uncomment-func-decl EVP_DigestFinal \
  --uncomment-func-decl EVP_MD_type \
  --uncomment-func-decl EVP_MD_size \
  --uncomment-func-decl EVP_MD_CTX_create \
  --uncomment-macro-redef 'DIGEST_R_[[:alnum:]_]*' \
  --uncomment-regex 'BORINGSSL_MAKE_DELETER(EVP_MD_CTX' \
  --sed "/^\/\/ using ScopedEVP_MD_CTX/ e cat $MYTMPDIR/ScopedEVP_MD_CTX.h" \
  --uncomment-func-decl EVP_MD_nid \
  --uncomment-func-decl EVP_Digest \
  --uncomment-func-decl EVP_MD_CTX_init \
  --uncomment-func-decl EVP_get_digestbyname
