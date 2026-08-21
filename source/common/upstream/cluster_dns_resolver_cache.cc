#include "source/common/upstream/cluster_dns_resolver_cache.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<Network::DnsResolverSharedPtr> ClusterDnsResolverCache::getOrCreate(
    Network::DnsResolverFactory& dns_resolver_factory, Event::Dispatcher& dispatcher, Api::Api& api,
    const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config) {
  ASSERT(dispatcher.isThreadSafe());

  // Sharing is limited to the c-ares resolver: its channel and query cache are the state worth
  // sharing, and limiting the scope keeps the other resolver types on their existing
  // one-per-cluster behavior.
  const bool shareable =
      dns_resolver_factory.name() == Network::CaresDnsResolver &&
      Runtime::runtimeFeatureEnabled("envoy.restart_features.shared_cares_dns_resolver");
  if (!shareable) {
    return dns_resolver_factory.createDnsResolver(dispatcher, api, typed_dns_resolver_config);
  }

  // Hashing the typed config rather than the unpacked resolver config means two configurations
  // that mean the same thing but do not serialize identically simply do not share. That is the
  // safe direction to err in.
  const std::size_t key = MessageUtil::hash(typed_dns_resolver_config);
  const auto it = resolvers_.find(key);
  if (it != resolvers_.end()) {
    if (auto resolver = it->second.lock(); resolver != nullptr) {
      ENVOY_LOG_MISC(trace, "reusing shared DNS resolver for config hash {}", key);
      return resolver;
    }
  }

  auto resolver_or_error =
      dns_resolver_factory.createDnsResolver(dispatcher, api, typed_dns_resolver_config);
  RETURN_IF_NOT_OK_REF(resolver_or_error.status());

  // Drop entries whose resolver is gone so the map does not grow as clusters come and go.
  absl::erase_if(resolvers_, [](const auto& entry) { return entry.second.expired(); });
  resolvers_[key] = resolver_or_error.value();
  ENVOY_LOG_MISC(trace, "created shared DNS resolver for config hash {}, cache size {}", key,
                 resolvers_.size());
  return resolver_or_error;
}

} // namespace Upstream
} // namespace Envoy
