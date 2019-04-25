#pragma once

#include "envoy/config/filter/http/tap/v2alpha/tap.pb.h"
#include "envoy/config/filter/http/tap/v2alpha/tap.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

/**
 * Config registration for the tap filter.
 */
class TapFilterFactory
    : public Common::FactoryBase<envoy::config::filter::http::tap::v2alpha::Tap> {
public:
  TapFilterFactory() : FactoryBase(HttpFilterNames::get().Tap) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::tap::v2alpha::Tap& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
