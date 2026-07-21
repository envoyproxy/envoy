#pragma once

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

/**
 * Config registration for the body size limiter filter.
 */
class BodySizeLimitFilterFactory
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit> {
public:
  BodySizeLimitFilterFactory() : DualFactoryBase("envoy.filters.http.body_size_limit") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
      const std::string& stats_prefix, DualInfo,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;
};

using UpstreamBodySizeLimitFilterFactory = BodySizeLimitFilterFactory;

DECLARE_FACTORY(BodySizeLimitFilterFactory);
DECLARE_FACTORY(UpstreamBodySizeLimitFilterFactory);

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
