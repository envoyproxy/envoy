#pragma once

#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/config/filter/http/router/v2/router.pb.validate.h"

#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class RouterFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::router::v2::Router> {
public:
  RouterFilterConfig() : FactoryBase(HttpFilterNames::get().Router) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stat_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::router::v2::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
