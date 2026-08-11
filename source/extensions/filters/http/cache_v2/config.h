#pragma once

#include "envoy/extensions/filters/http/cache_v2/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache_v2/v3/cache.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheFilterFactory : public Common::ExceptionFreeFactoryBase<
                               envoy::extensions::filters::http::cache_v2::v3::CacheV2Config> {
public:
  CacheFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.cache_v2") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths.
  static absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config& config,
                      Server::Configuration::ServerFactoryContext& context);
};

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
