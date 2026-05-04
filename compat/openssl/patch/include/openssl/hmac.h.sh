#!/bin/bash

set -euo pipefail

MYTMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$MYTMPDIR"' EXIT

cat > "$MYTMPDIR/ScopedHMAC_CTX.h" <<EOF
class ScopedHMAC_CTX {
 public:
  ScopedHMAC_CTX() : ctx_{HMAC_CTX_new()} {}
  ~ScopedHMAC_CTX() { HMAC_CTX_free(ctx_); }

  ScopedHMAC_CTX(const ScopedHMAC_CTX &) = delete;
  ScopedHMAC_CTX& operator=(const ScopedHMAC_CTX &) = delete;

  HMAC_CTX *get() { return ctx_; }
  const HMAC_CTX *get() const { return ctx_; }

  HMAC_CTX *operator->() { return ctx_; }
  const HMAC_CTX *operator->() const { return ctx_; }

  void Reset() {
    HMAC_CTX_free(ctx_);
    ctx_ = HMAC_CTX_new();
  }

 private:
  HMAC_CTX *ctx_;
};
EOF

uncomment.sh "$1" --comment -h \
  --uncomment-func-decl HMAC \
  --uncomment-func-decl HMAC_CTX_new \
  --uncomment-func-decl HMAC_CTX_free \
  --uncomment-func-decl HMAC_Init_ex \
  --uncomment-func-decl HMAC_Update \
  --uncomment-func-decl HMAC_Final \
  --sed "/^\/\/ using ScopedHMAC_CTX/ e cat $MYTMPDIR/ScopedHMAC_CTX.h"
