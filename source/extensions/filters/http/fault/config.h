#pragma once

#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * Config registration for the fault injection filter. @see NamedHttpFilterConfigFactory.
 */
class FaultFilterFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::HttpFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::fault::v2::HTTPFault()};
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::FactoryContext&) override;

  std::string name() override { return HttpFilterNames::get().FAULT; }

private:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const envoy::config::filter::http::fault::v2::HTTPFault& proto_config,
               const std::string& stats_prefix, Server::Configuration::FactoryContext& context);
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
