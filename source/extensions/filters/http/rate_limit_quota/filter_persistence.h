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

// GlobalTlsStores holds a singleton hash map of rate_limit_quota TLS stores,
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
  // Object to hold TLS slots after the factory itself has been cleaned up.
  struct TlsStore {
    TlsStore(Server::Configuration::FactoryContext& context, absl::string_view target_address,
             absl::string_view domain)
        : buckets_tls(context.serverFactoryContext().threadLocal()),
          target_address_(target_address), domain_(domain),
          main_dispatcher_(context.serverFactoryContext().mainThreadDispatcher()) {}

    ~TlsStore() {
      // Clean up the index from the global map. This is not thread-safe, so
      // it's only called after asserting that we're on the main thread.
      ASSERT_IS_MAIN_OR_TEST_THREAD();
      // The global client must be cleaned up by the server main thread before
      // it shuts down.
      if (global_client != nullptr) {
        main_dispatcher_.deferredDelete(std::move(global_client));
      }
      GlobalTlsStores::clearTlsStore(std::make_pair(target_address_, domain_));
    }

    std::unique_ptr<GlobalRateLimitClientImpl> global_client = nullptr;
    ThreadLocal::TypedSlot<ThreadLocalBucketsCache> buckets_tls;

  private:
    std::string target_address_;
    std::string domain_;
    Envoy::Event::Dispatcher& main_dispatcher_;
  };

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
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    stores().clear();
  }

private:
  // The index is a pair of <target_address, domain>.
  using TlsStoreIndex = std::pair<std::string, std::string>;

  // Map of rate_limit_quota TLS stores & looping garbage collection timer.
  using TlsStoreMap = absl::flat_hash_map<TlsStoreIndex, std::weak_ptr<TlsStore>>;

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

  // Clear a specified index when it is no longer captured by any filter factories.
  static void clearTlsStore(const TlsStoreIndex& index) { stores().erase(index); }
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_EXTENSIONS_FILTERS_HTTP_RATE_LIMIT_QUOTA_FILTER_PERSISTENCE_H_
