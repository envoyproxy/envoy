#pragma once

#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.h"
#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.validate.h"

#include "source/common/http/headers.h"
#include "source/extensions/http/injected_credentials/common/factory.h"
#include "source/extensions/http/injected_credentials/common/factory_base.h"
#include "source/extensions/http/injected_credentials/oauth2/client_credentials_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

using envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2;

class OAuth2CredentialInjectorFactory : public Common::CredentialInjectorFactoryBase<OAuth2> {
public:
  OAuth2CredentialInjectorFactory()
      : CredentialInjectorFactoryBase("envoy.http.injected_credentials.oauth2") {}
  Common::CredentialInjectorSharedPtr
  createOauth2ClientCredentialInjector(const OAuth2& proto_config, const std::string& stats_prefix,
                                       Server::Configuration::FactoryContext& context);
  Common::CredentialInjectorSharedPtr
  createCredentialInjectorFromProtoTyped(const OAuth2& config, const std::string& stats_prefix,
                                         Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(OAuth2CredentialInjectorFactory);

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
