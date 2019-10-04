#pragma once

#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.h"
#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {

/**
 * Config registration for the alpn filter.
 */
class AlpnConfigFactory
    : public Common::FactoryBase<envoy::config::filter::http::alpn::v2alpha::FilterConfig> {
public:
  AlpnConfigFactory() : FactoryBase(HttpFilterNames::get().Alpn) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::alpn::v2alpha::FilterConfig& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Alpn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
