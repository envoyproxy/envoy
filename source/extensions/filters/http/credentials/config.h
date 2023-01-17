#pragma once

#include <string>

#include "envoy/extensions/filters/http/credentials/v3alpha/injector.pb.h"
#include "envoy/extensions/filters/http/credentials/v3alpha/injector.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "source/extensions/filters/http/credentials/source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

class InjectorConfig : public Extensions::HttpFilters::Common::FactoryBase<
                         envoy::extensions::filters::http::credentials::v3alpha::Injector> {
public:
  InjectorConfig() : FactoryBase("envoy.filters.http.credentials") {}

  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::extensions::filters::http::credentials::v3alpha::Injector&,
                                    const std::string&,
                                    Server::Configuration::FactoryContext&) override;

private:
  CredentialSourcePtr createCredentialSource(const envoy::extensions::filters::http::credentials::v3alpha::Credential& proto_config,
                                             Server::Configuration::FactoryContext& context);
  CredentialSourcePtr createBasicAuthCredentialSource(const envoy::extensions::filters::http::credentials::v3alpha::BasicAuthCredential& proto_config,
                                             Server::Configuration::FactoryContext& context);
  CredentialSourcePtr createBearerTokenCredentialSource(const envoy::extensions::filters::http::credentials::v3alpha::BearerTokenCredential& proto_config,
                                             Server::Configuration::FactoryContext& context);
  CredentialSourcePtr createOauth2ClientCredentialsGrantCredentialSource(const envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential& proto_config,
                                             Server::Configuration::FactoryContext& context);
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
