#pragma once

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

/**
 * Config registration for the adaptive concurrency limit filter. @see NamedHttpFilterConfigFactory.
 */
class AdaptiveConcurrencyFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency> {
public:
  AdaptiveConcurrencyFilterFactory() : FactoryBase("envoy.filters.http.adaptive_concurrency") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
