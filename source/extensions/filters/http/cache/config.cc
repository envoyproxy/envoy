#include "source/extensions/filters/http/cache/config.h"

#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/cache/cache_policy/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using ::Envoy::Extensions::Cache::CachePolicyFactory;

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const std::string cache_type(
      TypeUtil::typeUrlToDescriptorFullName(config.typed_config().type_url()));
  HttpCacheFactory* const http_cache_factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(cache_type);
  if (http_cache_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", cache_type));
  }

  const std::string cache_policy_type(
      TypeUtil::typeUrlToDescriptorFullName(config.typed_cache_policy_config().type_url()));
  CachePolicyFactory* const cache_policy_factory =
      Registry::FactoryRegistry<CachePolicyFactory>::getFactoryByType(cache_policy_type);
  if (cache_policy_factory == nullptr) {
    throw EnvoyException(
        fmt::format("Didn't find a registered implementation for type: '{}'", cache_policy_type));
  }
  ProtobufTypes::MessagePtr cache_policy_config = cache_policy_factory->createEmptyConfigProto();
  MessageUtil::unpackTo(config.typed_cache_policy_config(), *cache_policy_config);
  // Use a shared_ptr here because FilterFactoryCb is reusable, so we cannot
  // use CallAtMostOnce to move the unique_ptr into the lambda.
  std::shared_ptr<Protobuf::Message> shared_cache_policy_config(cache_policy_config.release());

  return [config, stats_prefix, &context, http_cache_factory, cache_policy_factory,
          policy_config =
              shared_cache_policy_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheFilter>(
        config, stats_prefix, context.scope(), context.timeSource(),
        http_cache_factory->getCache(config, context),
        cache_policy_factory->createCachePolicyFromProto(*policy_config)));
  };
}

// Static registration for CacheFilter.
static Registry::RegisterFactory<CacheFilterFactory,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_; // NOLINT Envoy-style factory registration

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
