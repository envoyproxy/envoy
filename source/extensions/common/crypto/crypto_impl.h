#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <vector>

#include "envoy/common/crypto/crypto.h"

#include "openssl/base.h"
#include "openssl/evp.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class PublicKeyWrapper : public Envoy::Common::Crypto::CryptoObject {
public:
  PublicKeyWrapper() = default;
  PublicKeyWrapper(EVP_PKEY* pkey) : pkey_(pkey) {}
  PublicKeyWrapper(const PublicKeyWrapper& pkeyWrapper);
  EVP_PKEY* getEVP_PKEY() const;
  void setEVP_PKEY(EVP_PKEY* pkey);

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

} // namespace Crypto
} // namespace Common
} // namespace Envoy
