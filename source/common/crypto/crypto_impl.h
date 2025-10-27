#pragma once

#include "envoy/common/crypto/crypto.h"

#include "openssl/base.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class PKeyObject : public Envoy::Common::Crypto::CryptoObject {
public:
  PKeyObject() = default;
  PKeyObject(EVP_PKEY* pkey) : pkey_(pkey) {}
  EVP_PKEY* getEVP_PKEY() const;
  void setEVP_PKEY(EVP_PKEY* pkey);

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

using PKeyObjectPtr = std::unique_ptr<PKeyObject>;

} // namespace Crypto
} // namespace Common
} // namespace Envoy
