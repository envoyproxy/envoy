#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/http/injected_credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Common {

class NamedCredentialInjectorConfigFactory : public Config::TypedFactory {
public:
  virtual CredentialInjectorSharedPtr
  createCredentialInjectorFromProto(const Protobuf::Message& config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.injected_credentials"; }
};

} // namespace Common
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
