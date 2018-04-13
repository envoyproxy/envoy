#pragma once

#include "envoy/config/filter/http/health_check/v2/health_check.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HealthCheck {

class HealthCheckFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string&,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::config::filter::http::health_check::v2::HealthCheck()};
  }

  std::string name() override { return HttpFilterNames::get().HEALTH_CHECK; }

private:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::health_check::v2::HealthCheck& proto_config,
               const std::string& stats_prefix, Server::Configuration::FactoryContext& context);
};

} // namespace HealthCheck
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
