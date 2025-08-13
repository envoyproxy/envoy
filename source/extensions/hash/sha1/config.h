#pragma once

#include "envoy/registry/registry.h"

#include "source/extensions/hash/factory.h"
#include "source/extensions/hash/sha1/algorithm_impl.h"

namespace Envoy {
namespace Extensions {
namespace Hash {

class SHA1AlgorithmProviderFactory : public NamedAlgorithmProviderConfigFactory {
public:
  SHA1AlgorithmProviderFactory() : NamedAlgorithmProviderConfigFactory("envoy.hash.sha1") {}

  AlgorithmProviderSharedPtr createAlgorithmProvider() override;
};

DECLARE_FACTORY(SHA1AlgorithmProviderFactory);

} // namespace Hash
} // namespace Extensions
} // namespace Envoy
