#pragma once

#include "envoy/extensions/common/matching/v3/extension_matcher.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace MatchWrapper {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class MatchWrapperConfig
    : public Common::FactoryBase<envoy::extensions::common::matching::v3::ExtensionWithMatcher> {
public:
  MatchWrapperConfig() : FactoryBase(HttpFilterNames::get().MatchingFilter) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::common::matching::v3::ExtensionWithMatcher& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override;
};

} // namespace MatchWrapper
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
