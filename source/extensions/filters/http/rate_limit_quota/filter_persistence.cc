#include "source/extensions/filters/http/rate_limit_quota/filter_persistence.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "envoy/grpc/async_client_manager.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using TlsStore = GlobalTlsStores::TlsStore;

// Helper to initialize a new TLS store based on a rate_limit_quota config's
// settings.
absl::StatusOr<std::shared_ptr<TlsStore>>
initTlsStore(const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
             Server::Configuration::FactoryContext& context, absl::string_view target_address,
             absl::string_view domain) {
  // Quota bucket & global client TLS objects are created with the config and
  // kept alive via shared_ptr to a storage struct. The local rate limit client
  // in each filter instance assumes that the slot will outlive them.
  std::shared_ptr<TlsStore> tls_store = std::make_shared<TlsStore>(context, target_address, domain);
  auto tl_buckets_cache =
      std::make_shared<ThreadLocalBucketsCache>(std::make_shared<BucketsCache>());
  tls_store->buckets_tls.set(
      [tl_buckets_cache]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
        return tl_buckets_cache;
      });

  // TODO(bsurber): Implement report timing & usage aggregation based on each
  // bucket's reporting_interval field. Currently this is not supported and all
  // usage is reported on a hardcoded interval.
  std::chrono::milliseconds reporting_interval(5000);

  // Create the global client resource to be shared via TLS to all worker
  // threads (accessed through a filter-specific LocalRateLimitClient).
  auto tl_global_client_or = createGlobalRateLimitClientImpl(
      context, domain, reporting_interval, tls_store->buckets_tls, config_with_hash_key);
  RETURN_IF_NOT_OK_REF(tl_global_client_or.status());
  tls_store->global_client = std::move(tl_global_client_or.value());

  return tls_store;
}

// References a statically shared map. This is not thread-safe so it should
// only be called during RLQS filter factory creation on the main thread.
absl::StatusOr<std::shared_ptr<TlsStore>>
GlobalTlsStores::getTlsStore(const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                             Server::Configuration::FactoryContext& context,
                             absl::string_view target_address, absl::string_view domain) {
  TlsStoreIndex index = std::make_pair(std::string(target_address), std::string(domain));
  // Find existing TlsStore or initialize a new one.
  auto it = stores().find(index);
  if (it != stores().end()) {
    ENVOY_LOG(debug, "Found existing cache & RLQS client for target ({}) and domain ({}).",
              index.first, index.second);
    return it->second.lock();
  }
  ENVOY_LOG(debug, "Creating a new cache & RLQS client for target ({}) and domain ({}).",
            index.first, index.second);
  auto tls_store_or = initTlsStore(config_with_hash_key, context, index.first, index.second);
  RETURN_IF_NOT_OK_REF(tls_store_or.status());
  std::shared_ptr<TlsStore> tls_store = std::move(tls_store_or.value());
  // Save weak_ptr as an unowned reference.
  stores()[index] = tls_store;
  return tls_store;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
