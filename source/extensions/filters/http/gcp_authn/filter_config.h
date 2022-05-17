#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

// class TokenCacheImpl {
// public:
//   explicit TokenCacheImpl(
//       const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config)
//       : size_(config.cache_size()) {
//     // cache_size_ = config_.cache_size();
//     // lru_cache_ =
//     //     std::make_unique<SimpleLRUCache<std::string, std::string>>(cache_size_);
//   }
//   int size() { return size_; }

// private:
//   // std::unique_ptr<SimpleLRUCache<std::string, std::string>> lru_cache_;
//   //  TokenCacheConfig config_;
//   //  Envoy::TimeSource& time_source_;
//   //absl::node_hash<std::string, std::string> token_map_;
//   int size_;
// };

// class ThreadLocalCache : public Envoy::ThreadLocal::ThreadLocalObject {
// public:
//   explicit ThreadLocalCache(
//       envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig config)
//       : cache_(config) {}
//   TokenCacheImpl& getCache() { return cache_; }

// private:
//   TokenCacheImpl cache_;
// };

// class TokenCache {
// public:
//   TokenCache(const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
//              Envoy::Server::Configuration::FactoryContext& context)
//       : tls(context.threadLocal()) {
//     tls.set([config](Envoy::Event::Dispatcher&) {
//       // TODO(tyxia) why make_shared here????
//       return std::make_shared<ThreadLocalCache>(config.cache_config());
//     });
//   }
//   Envoy::ThreadLocal::TypedSlot<ThreadLocalCache> tls;
// };

class GcpAuthnFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  GcpAuthnFilterFactory() : FactoryBase(std::string(FilterName)) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
