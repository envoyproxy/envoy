#pragma once

#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class RouterFilterConfig : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stat_prefix,
                      Server::Configuration::FactoryContext& context) override;

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stat_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::router::v2::Router()};
  }

  std::string name() override { return HttpFilterNames::get().ROUTER; }

private:
  Http::FilterFactoryCb
  createFilter(const envoy::config::filter::http::router::v2::Router& proto_config,
               const std::string& stat_prefix, Server::Configuration::FactoryContext& context);
};

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
