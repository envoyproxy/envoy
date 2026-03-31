#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/network/dns_resolver/hickory/v3/hickory_dns_resolver.pb.h"
#include "envoy/network/dns.h"
#include "envoy/network/dns_resolver.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Network {

class HickoryDnsResolver;

/**
 * All Hickory DNS resolver stats. @see stats_macros.h
 */
#define ALL_HICKORY_DNS_RESOLVER_STATS(COUNTER, GAUGE)                                             \
  COUNTER(resolve_total)                                                                           \
  GAUGE(pending_resolutions, NeverImport)                                                          \
  COUNTER(not_found)                                                                               \
  COUNTER(get_addr_failure)                                                                        \
  COUNTER(timeouts)

/**
 * Struct definition for all Hickory DNS resolver stats. @see stats_macros.h
 */
struct HickoryDnsResolverStats {
  ALL_HICKORY_DNS_RESOLVER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// Function pointer types for the DNS resolver ABI event hooks.
using OnDnsResolverConfigNewType = decltype(&envoy_dynamic_module_on_dns_resolver_config_new);
using OnDnsResolverConfigDestroyType =
    decltype(&envoy_dynamic_module_on_dns_resolver_config_destroy);
using OnDnsResolverNewType = decltype(&envoy_dynamic_module_on_dns_resolver_new);
using OnDnsResolverDestroyType = decltype(&envoy_dynamic_module_on_dns_resolver_destroy);
using OnDnsResolveType = decltype(&envoy_dynamic_module_on_dns_resolve);
using OnDnsResolveCancelType = decltype(&envoy_dynamic_module_on_dns_resolve_cancel);
using OnDnsResolverResetNetworkingType =
    decltype(&envoy_dynamic_module_on_dns_resolver_reset_networking);

/**
 * Configuration for the Hickory DNS resolver. Loads the dynamic module, resolves ABI function
 * pointers, and holds the in-module configuration. Shared across all resolver instances created
 * from this config.
 */
class HickoryDnsResolverConfig {
public:
  static std::shared_ptr<HickoryDnsResolverConfig>
  create(const envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig&
             proto_config);

  ~HickoryDnsResolverConfig();

  // Resolved function pointers from the dynamic module.
  OnDnsResolverConfigNewType on_dns_resolver_config_new_ = nullptr;
  OnDnsResolverConfigDestroyType on_dns_resolver_config_destroy_ = nullptr;
  OnDnsResolverNewType on_dns_resolver_new_ = nullptr;
  OnDnsResolverDestroyType on_dns_resolver_destroy_ = nullptr;
  OnDnsResolveType on_dns_resolve_ = nullptr;
  OnDnsResolveCancelType on_dns_resolve_cancel_ = nullptr;
  OnDnsResolverResetNetworkingType on_dns_resolver_reset_networking_ = nullptr;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_dns_resolver_config_module_ptr in_module_config_ = nullptr;

private:
  HickoryDnsResolverConfig() = default;

  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using HickoryDnsResolverConfigSharedPtr = std::shared_ptr<HickoryDnsResolverConfig>;

/**
 * Active DNS query that delegates to the in-module query via the ABI.
 */
class HickoryPendingResolution : public ActiveDnsQuery {
public:
  HickoryPendingResolution(HickoryDnsResolver& parent, DnsResolver::ResolveCb callback,
                           uint64_t query_id, const std::string& dns_name);

  // ActiveDnsQuery
  void cancel(CancelReason reason) override;
  void addTrace(uint8_t) override {}
  std::string getTraces() override { return {}; }

  // Set by the parent after the ABI call returns.
  envoy_dynamic_module_type_dns_query_module_ptr query_module_ptr_ = nullptr;

  DnsResolver::ResolveCb callback_;
  const uint64_t query_id_;
  const std::string dns_name_;
  bool cancelled_ = false;
  HickoryDnsResolver& parent_;
};

/**
 * DNS resolver implementation that delegates to a Rust module via the Dynamic Modules ABI.
 * This is a thread-safe resolver: resolve() is called on the dispatcher thread, and the
 * module may deliver results from any thread. The shell posts results back to the correct
 * dispatcher thread.
 */
class HickoryDnsResolver : public DnsResolver, protected Logger::Loggable<Logger::Id::dns> {
public:
  HickoryDnsResolver(HickoryDnsResolverConfigSharedPtr config, Event::Dispatcher& dispatcher,
                     Stats::Scope& root_scope);
  ~HickoryDnsResolver() override;

  static HickoryDnsResolverStats generateHickoryDnsResolverStats(Stats::Scope& scope);

  // DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;
  void resetNetworking() override;

  /**
   * Called by the ABI callback (from any thread) to deliver resolution results.
   * Posts to the dispatcher thread for thread safety.
   */
  void onResolveComplete(uint64_t query_id, envoy_dynamic_module_type_dns_resolution_status status,
                         absl::string_view details, std::list<DnsResponse>&& response);

private:
  friend class HickoryPendingResolution;
  friend void ::envoy_dynamic_module_callback_dns_resolve_complete(
      envoy_dynamic_module_type_dns_resolver_envoy_ptr, uint64_t,
      envoy_dynamic_module_type_dns_resolution_status, envoy_dynamic_module_type_module_buffer,
      const envoy_dynamic_module_type_dns_address*, size_t);

  static envoy_dynamic_module_type_dns_lookup_family
  toLookupFamily(DnsLookupFamily dns_lookup_family);

  void chargeGetAddrInfoErrorStats(absl::string_view details);

  HickoryDnsResolverConfigSharedPtr config_;
  Event::Dispatcher& dispatcher_;
  envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr_;
  uint64_t next_query_id_ = 1;
  absl::flat_hash_map<uint64_t, HickoryPendingResolution*> pending_queries_;
  // Checked by the ABI callback from `Tokio` threads to avoid posting to the dispatcher
  // after the resolver begins destruction.
  std::atomic<bool> shutting_down_{false};
  Stats::ScopeSharedPtr scope_;
  HickoryDnsResolverStats stats_;
};

/**
 * Factory for creating HickoryDnsResolver instances.
 */
class HickoryDnsResolverFactory : public DnsResolverFactory,
                                  public Logger::Loggable<Logger::Id::dns> {
public:
  std::string name() const override { return {"envoy.network.dns_resolver.hickory"}; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig()};
  }

  absl::StatusOr<DnsResolverSharedPtr> createDnsResolver(
      Event::Dispatcher& dispatcher, Api::Api& api,
      const envoy::config::core::v3::TypedExtensionConfig& typed_config) const override;
};

DECLARE_FACTORY(HickoryDnsResolverFactory);

} // namespace Network
} // namespace Envoy
