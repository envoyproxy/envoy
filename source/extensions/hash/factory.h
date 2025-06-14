#pragma once

#include "envoy/config/typed_config.h"

#include "source/extensions/hash/algorithm_provider.h"

namespace Envoy {
namespace Extensions {
namespace Hash {

class NamedAlgorithmProviderConfigFactory : public Config::UntypedFactory {
public:
  virtual AlgorithmProviderSharedPtr createAlgorithmProvider() PURE;

  std::string name() const override { return name_; }

  std::string category() const override { return "envoy.hash"; }

protected:
  NamedAlgorithmProviderConfigFactory(const std::string& name) : name_(name) {}

private:
  const std::string name_;
};

} // namespace Hash
} // namespace Extensions
} // namespace Envoy
