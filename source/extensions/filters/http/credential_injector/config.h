#pragma once

#include "envoy/extensions/filters/http/credential_injector/v3/credential_injector.pb.h"
#include "envoy/extensions/filters/http/credential_injector/v3/credential_injector.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

class CredentialInjectorFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::credential_injector::v3::CredentialInjector> {
public:
  CredentialInjectorFilterFactory() : FactoryBase("envoy.filters.http.credential_injector") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::credential_injector::v3::CredentialInjector& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
