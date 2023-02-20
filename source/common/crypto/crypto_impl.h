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
  PublicKeyObject(const PublicKeyObject& pkey_wrapper);
  EVP_PKEY* getEvpPkey() const;
  void setEvpPkey(EVP_PKEY* pkey);

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
