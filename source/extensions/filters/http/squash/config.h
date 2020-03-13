#pragma once

#include "envoy/extensions/filters/http/squash/v3/squash.pb.h"
#include "envoy/extensions/filters/http/squash/v3/squash.pb.validate.h"

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
    : public Common::FactoryBase<envoy::extensions::filters::http::squash::v3::Squash> {
public:
  SquashFilterConfigFactory() : FactoryBase(HttpFilterNames::get().Squash) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::squash::v3::Squash& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
