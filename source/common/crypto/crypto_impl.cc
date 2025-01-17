#include "source/common/crypto/crypto_impl.h"

namespace Envoy {
namespace Common {
namespace Crypto {

EVP_PKEY* PublicKeyObject::getEVP_PKEY() const { return pkey_.get(); }

void PublicKeyObject::setEVP_PKEY(EVP_PKEY* pkey) { pkey_.reset(pkey); }

} // namespace Crypto
} // namespace Common
} // namespace Envoy
