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
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency> {
public:
  AdaptiveConcurrencyFilterFactory()
      : DualFactoryBase("envoy.filters.http.adaptive_concurrency") {}

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      const std::string& stats_prefix, DualInfo info,
      Server::Configuration::ServerFactoryContext& context) override;

private:
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  static Http::FilterFactoryCb createFilterFactory(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& scope);
};

using UpstreamAdaptiveConcurrencyFilterFactory = AdaptiveConcurrencyFilterFactory;
DECLARE_FACTORY(AdaptiveConcurrencyFilterFactory);
DECLARE_FACTORY(UpstreamAdaptiveConcurrencyFilterFactory);

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
