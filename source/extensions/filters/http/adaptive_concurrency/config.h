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
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency> {
public:
  AdaptiveConcurrencyFilterFactory()
      : UnifiedFactoryBase("envoy.filters.http.adaptive_concurrency") {}

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    return createFilterFactory(proto_config, extra_context.stats_prefix, context,
                               extra_context.scopeOr(context));
  }

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context,
      Stats::Scope& scope);
};

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
