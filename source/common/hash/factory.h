#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/hash/algorithm_provider.h"

namespace Envoy {
namespace Hash {

class NamedCredentialInjectorConfigFactory : public Config::TypedFactory {
public:
  virtual AlgorithmProviderSharedPtr createAlgorithmProviderFromProto(const Protobuf::Message& config) PURE;
};

} // namespace Hash
} // namespace Envoy
