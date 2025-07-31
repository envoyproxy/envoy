#pragma once

#ifndef THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_FILTERS_HTTP_RATE_LIMIT_QUOTA_FILTER_PERSISTENCE_H_
#define THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_FILTERS_HTTP_RATE_LIMIT_QUOTA_FILTER_PERSISTENCE_H_

#include <memory>
#include <string>
#include <utility>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/base/no_destructor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// Object to hold TLS slots after the factory itself has been cleaned up.
struct TlsStore : public Event::DeferredDeletable {
  TlsStore(Server::Configuration::FactoryContext& context)
      : buckets_tls(context.serverFactoryContext().threadLocal()) {}

  std::unique_ptr<GlobalRateLimitClientImpl> global_client = nullptr;
  ThreadLocal::TypedSlot<ThreadLocalBucketsCache> buckets_tls;

  // Timer checking this index in the static map of TLS stores. If this store is
  // no longer in-use by a filter factory, then it will be cleaned up.
  Event::TimerPtr garbage_collector;
};

// GlobalTlsStores holds a singleton hashmap of rate_limit_quota TLS stores,
// indexed by their combined RLQS server targets & domains.
//
// This follows the data sharing model of FactoryRegistry, and similarly does
// not guarantee thread-safety. Additions or removals of indices can only be
// done on the main thread, as part of filter factory creation and garbage
// collection respectively.
//
// Note, multiple RLQS clients with different configs (e.g. timeouts) can hit
// the same index (destination + domain). The global map does not guarantee
// which config will be selected for the client creation.
class GlobalTlsStores : public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  // Get an existing TLS store by index, or create one if not found.
  static std::shared_ptr<TlsStore>
  getTlsStore(Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
              Server::Configuration::FactoryContext& context, absl::string_view target_address,
              absl::string_view domain);

  // Test-only: only thread-safe if filter factories are stable.
  static size_t size() { return stores().size(); }

  // Test-only: unsafely clear the global map, used in testing to reset static
  // state. A safer alternative is to delete all rate_limit_quota filters from
  // config with LDS & let the garbage collector handle cleanup.
  static void clear() {
    // Ensure all watcher timers are disabled first.
    for (auto& [index, tls_store] : stores()) {
      if (tls_store->garbage_collector->enabled()) {
        tls_store->garbage_collector->disableTimer();
      }
    }
    // Wipe out all indices.
    stores().clear();
  }

private:
  // The index is a pair of <target_address, domain>.
  using TlsStoreIndex = std::pair<std::string, std::string>;

  // Map of rate_limit_quota TLS stores & looping garbage collection timer.
  using TlsStoreMap = absl::flat_hash_map<TlsStoreIndex, std::shared_ptr<TlsStore>>;

  // Static reference to shared map of rate_limit_quota TLS stores (follows the
  // data sharing model of FactoryRegistry::factories()).
  static TlsStoreMap& stores() {
    static absl::NoDestructor<TlsStoreMap> tls_stores{};
    return *tls_stores;
  }

  // Find or initialize a TLS store for the given config.
  static std::shared_ptr<TlsStore>
  getTlsStoreImpl(Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                  Server::Configuration::FactoryContext& context, TlsStoreIndex& index,
                  bool* new_store_out);

  // Setup the index's garbage collection timer. This does not enable the timer
  // itself, just returns the static reference. The context input is only used
  // for Timer initialization so does not need to be provided if the Timer is
  // already guaranteed to exist.
  static void initGarbageCollector(Server::Configuration::FactoryContext& context,
                                   TlsStoreIndex& index, TlsStore* expected_tls_store);

  // Clear a specified index when it is no longer captured by any filter factory
  // cbs.
  static void clearTlsStore(TlsStoreIndex& index) { stores().erase(index); }
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_FILTERS_HTTP_RATE_LIMIT_QUOTA_FILTER_PERSISTENCE_H_
