#pragma once

#include "envoy/common/crypto/crypto.h"

#include "openssl/base.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class PublicKeyObject : public Envoy::Common::Crypto::CryptoObject {
public:
  PublicKeyObject() = default;
  PublicKeyObject(EVP_PKEY* pkey) : pkey_(pkey) {}
  EVP_PKEY* getEVP_PKEY() const;
  void setEVP_PKEY(EVP_PKEY* pkey);

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class PrivateKeyObject : public Envoy::Common::Crypto::CryptoObject {
public:
  PrivateKeyObject() = default;
  PrivateKeyObject(EVP_PKEY* pkey) : pkey_(pkey) {}
  EVP_PKEY* getEVP_PKEY() const;
  void setEVP_PKEY(EVP_PKEY* pkey);

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
