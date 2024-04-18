#pragma once

#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

/**
 * Config registration for the tap filter.
 */
class TapFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::tap::v3::Tap> {
public:
  TapFilterFactory() : FactoryBase("envoy.filters.http.tap") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::tap::v3::Tap& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
