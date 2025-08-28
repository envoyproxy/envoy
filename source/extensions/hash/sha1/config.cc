#include "source/extensions/hash/sha1/config.h"

namespace Envoy {
namespace Extensions {
namespace Hash {

AlgorithmProviderSharedPtr SHA1AlgorithmProviderFactory::createAlgorithmProvider() {
  return std::make_shared<SHA1AlgorithmImpl>();
}

REGISTER_FACTORY(SHA1AlgorithmProviderFactory, NamedAlgorithmProviderConfigFactory);

} // namespace Hash
} // namespace Extensions
} // namespace Envoy
