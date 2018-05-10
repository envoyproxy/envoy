#pragma once

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class FilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& config, const std::string&,
                      Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config, const std::string&,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication};
  }

  std::string name() override { return HttpFilterNames::get().JWT_AUTHN; }

private:
  Http::FilterFactoryCb createFilter(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& proto_config,
      Server::Configuration::FactoryContext& context);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
