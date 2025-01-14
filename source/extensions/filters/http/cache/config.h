#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterFactory
    : public Common::DualFactoryBase<envoy::extensions::filters::http::cache::v3::CacheConfig> {
public:
  CacheFilterFactory() : DualFactoryBase("envoy.filters.http.cache") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
      const std::string& stats_prefix, DualInfo,
      Server::Configuration::ServerFactoryContext& context) override;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
