#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/injected_credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace InjectedCredentials {
namespace Common {

class NamedCredentialInjectorConfigFactory : public Config::TypedFactory {
public:
  ~NamedCredentialInjectorConfigFactory() override = default;

  virtual CredentialInjectorSharedPtr
  createCredentialInjectorFromProto(const Protobuf::Message& config,
                                    Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.injected_credentials"; }
};

} // namespace Common
} // namespace InjectedCredentials
} // namespace Extensions
} // namespace Envoy
