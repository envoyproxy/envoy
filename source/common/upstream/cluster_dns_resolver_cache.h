#pragma once

#include <cstddef>
#include <memory>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns_resolver.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Upstream {

/**
 * Caches the DNS resolvers created for clusters, so that clusters configured with an identical
 * resolver configuration share a single resolver and, with it, the resolver's query cache.
 *
 * This deliberately lives on the cluster manager factory rather than on the DNS resolver factory:
 *
 * - It is owned per server, so two servers running in the same process (see
 *   test/integration/multi_envoy_test.cc) cannot share resolvers with each other.
 * - It is only ever used while creating a cluster, which happens on the main thread with the main
 *   thread dispatcher, so it needs no locking and every cached resolver is bound to the one
 *   dispatcher that all of its users run on.
 * - Nothing outside cluster creation can reach it, so callers that must stay isolated - notably
 *   the UDP DNS filter, which is constructed once per worker thread - keep getting their own
 *   resolver from DnsResolverFactory::createDnsResolver().
 *
 * Sharing is limited to the c-ares resolver, whose channel and query cache are what the sharing is
 * for, and can be disabled entirely with the "envoy.restart_features.shared_cares_dns_resolver"
 * runtime guard.
 */
class ClusterDnsResolverCache {
public:
  /**
   * @return a DNS resolver for the given configuration, either newly created or shared with
   *         another cluster that was created with an identical configuration.
   * @param dns_resolver_factory the factory resolved from typed_dns_resolver_config.
   * @param dispatcher the main thread dispatcher, which the resolver is bound to.
   * @param api API interface to interact with system resources.
   * @param typed_dns_resolver_config the resolver configuration.
   */
  absl::StatusOr<Network::DnsResolverSharedPtr>
  getOrCreate(Network::DnsResolverFactory& dns_resolver_factory, Event::Dispatcher& dispatcher,
              Api::Api& api,
              const envoy::config::core::v3::TypedExtensionConfig& typed_dns_resolver_config);

private:
  // Keyed on the hash of the typed resolver config. Entries are weak so that a resolver is
  // released once the last cluster using it goes away, and so the map does not grow without bound
  // as clusters churn.
  absl::flat_hash_map<std::size_t, std::weak_ptr<Network::DnsResolver>> resolvers_;
};

} // namespace Upstream
} // namespace Envoy
