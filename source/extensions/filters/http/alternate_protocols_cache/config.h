#pragma once

#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

/**
 * Config registration for the alternate protocols cache filter.
 */
class AlternateProtocolsCacheFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig> {
public:
  AlternateProtocolsCacheFilterFactory()
      : FactoryBase(HttpFilterNames::get().AlternateProtocolsCache) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(AlternateProtocolsCacheFilterFactory);

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
