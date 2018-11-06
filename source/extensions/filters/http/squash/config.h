#pragma once

#include "envoy/config/filter/http/squash/v2/squash.pb.h"
#include "envoy/config/filter/http/squash/v2/squash.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {

/**
 * Config registration for the squash filter. @see NamedHttpFilterConfigFactory.
 */
class SquashFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::http::squash::v2::Squash> {
public:
  SquashFilterConfigFactory() : FactoryBase(HttpFilterNames::get().Squash) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string&,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::squash::v2::Squash& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
