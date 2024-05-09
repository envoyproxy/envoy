#pragma once

#include "envoy/extensions/http/injected_credentials/generic/v3/generic.pb.h"
#include "envoy/extensions/http/injected_credentials/generic/v3/generic.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/http/injected_credentials/common/factory.h"
#include "source/extensions/http/injected_credentials/common/factory_base.h"
#include "source/extensions/http/injected_credentials/generic/generic_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Generic {

using envoy::extensions::http::injected_credentials::generic::v3::Generic;

class GenericCredentialInjectorFactory : public Common::CredentialInjectorFactoryBase<Generic> {
public:
  GenericCredentialInjectorFactory()
      : CredentialInjectorFactoryBase("envoy.http.injected_credentials.generic") {}

private:
  Common::CredentialInjectorSharedPtr
  createCredentialInjectorFromProtoTyped(const Generic& config, const std::string& stats_prefix,
                                         Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(GenericCredentialInjectorFactory);

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
