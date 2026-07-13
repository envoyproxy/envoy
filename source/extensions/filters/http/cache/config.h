#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache/v3/cache.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilterFactory : public Common::ExceptionFreeFactoryBase<
                               envoy::extensions::filters::http::cache::v3::CacheConfig> {
public:
  CacheFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.cache") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
