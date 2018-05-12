#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

/**
 * Config registration for the RBAC filter. @see NamedHttpFilterConfigFactory.
 */
class RBACFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  Http::FilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED;
  }

  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::rbac::v2::RBAC()};
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::http::rbac::v2::RBACPerRoute()};
  }

  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message& proto_config,
                                  Server::Configuration::FactoryContext& context) override;

  std::string name() override { return HttpFilterNames::get().RBAC; }
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
