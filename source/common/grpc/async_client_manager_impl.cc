#include "source/common/grpc/async_client_manager_impl.h"

#include <chrono>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/base64.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "source/common/grpc/google_async_client_impl.h"
#endif

namespace Envoy {
namespace Grpc {
namespace {

constexpr uint64_t DefaultEntryIdleDuration{50000};

// Validates a string for gRPC header key compliance. This is a subset of legal HTTP characters.
// See https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
bool validateGrpcHeaderChars(absl::string_view key) {
  for (auto ch : key) {
    if (!(absl::ascii_isalnum(ch) || ch == '_' || ch == '.' || ch == '-')) {
      return false;
    }
  }
  return true;
}

bool validateGrpcCompatibleAsciiHeaderValue(absl::string_view h_value) {
  for (auto ch : h_value) {
    if (ch < 0x20 || ch > 0x7e) {
      return false;
    }
  }
  return true;
}

} // namespace

AsyncClientFactoryImpl::AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                                               const envoy::config::core::v3::GrpcService& config,
                                               bool skip_cluster_check, TimeSource& time_source)
    : cm_(cm), config_(config), time_source_(time_source) {
  if (skip_cluster_check) {
    return;
  }
  cm_.checkActiveStaticCluster(config.envoy_grpc().cluster_name());
}

AsyncClientManagerImpl::AsyncClientManagerImpl(
    Upstream::ClusterManager& cm, ThreadLocal::Instance& tls, TimeSource& time_source,
    Api::Api& api, const StatNames& stat_names,
    const envoy::config::bootstrap::v3::Bootstrap::GrpcAsyncClientManagerConfig& config)
    : cm_(cm), tls_(tls), time_source_(time_source), api_(api), stat_names_(stat_names),
      raw_async_client_cache_(tls_) {

  const auto max_cached_entry_idle_duration = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(config, max_cached_entry_idle_duration, DefaultEntryIdleDuration));

  raw_async_client_cache_.set([max_cached_entry_idle_duration](Event::Dispatcher& dispatcher) {
    return std::make_shared<RawAsyncClientCache>(dispatcher, max_cached_entry_idle_duration);
  });
#ifdef ENVOY_GOOGLE_GRPC
  google_tls_slot_ = tls.allocateSlot();
  google_tls_slot_->set(
      [&api](Event::Dispatcher&) { return std::make_shared<GoogleAsyncClientThreadLocal>(api); });
#else
  UNREFERENCED_PARAMETER(api_);
#endif
}

RawAsyncClientPtr AsyncClientFactoryImpl::createUncachedRawAsyncClient() {
  return std::make_unique<AsyncClientImpl>(cm_, config_, time_source_);
}

GoogleAsyncClientFactoryImpl::GoogleAsyncClientFactoryImpl(
    ThreadLocal::Instance& tls, ThreadLocal::Slot* google_tls_slot, Stats::Scope& scope,
    const envoy::config::core::v3::GrpcService& config, Api::Api& api, const StatNames& stat_names)
    : tls_(tls), google_tls_slot_(google_tls_slot),
      scope_(scope.createScope(fmt::format("grpc.{}.", config.google_grpc().stat_prefix()))),
      config_(config), api_(api), stat_names_(stat_names) {

#ifndef ENVOY_GOOGLE_GRPC
  UNREFERENCED_PARAMETER(tls_);
  UNREFERENCED_PARAMETER(google_tls_slot_);
  UNREFERENCED_PARAMETER(scope_);
  UNREFERENCED_PARAMETER(config_);
  UNREFERENCED_PARAMETER(api_);
  UNREFERENCED_PARAMETER(stat_names_);
  throwEnvoyExceptionOrPanic("Google C++ gRPC client is not linked");
#else
  ASSERT(google_tls_slot_ != nullptr);
#endif

  // Check metadata for gRPC API compliance. Uppercase characters are lowered in the HeaderParser.
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
  for (const auto& header : config.initial_metadata()) {
    // Validate key
    if (!validateGrpcHeaderChars(header.key())) {
      throwEnvoyExceptionOrPanic(
          fmt::format("Illegal characters in gRPC initial metadata header key: {}.", header.key()));
    }

    // Validate value
    // Binary base64 encoded - handled by the GRPC library
    if (!::absl::EndsWith(header.key(), "-bin") &&
        !validateGrpcCompatibleAsciiHeaderValue(header.value())) {
      throwEnvoyExceptionOrPanic(fmt::format(
          "Illegal ASCII value for gRPC initial metadata header key: {}.", header.key()));
    }
  }
}

RawAsyncClientPtr GoogleAsyncClientFactoryImpl::createUncachedRawAsyncClient() {
#ifdef ENVOY_GOOGLE_GRPC
  GoogleGenericStubFactory stub_factory;
  return std::make_unique<GoogleAsyncClientImpl>(
      tls_.dispatcher(), google_tls_slot_->getTyped<GoogleAsyncClientThreadLocal>(), stub_factory,
      scope_, config_, api_, stat_names_);
#else
  return nullptr;
#endif
}

AsyncClientFactoryPtr
AsyncClientManagerImpl::factoryForGrpcService(const envoy::config::core::v3::GrpcService& config,
                                              Stats::Scope& scope, bool skip_cluster_check) {
  switch (config.target_specifier_case()) {
  case envoy::config::core::v3::GrpcService::TargetSpecifierCase::kEnvoyGrpc:
    return std::make_unique<AsyncClientFactoryImpl>(cm_, config, skip_cluster_check, time_source_);
  case envoy::config::core::v3::GrpcService::TargetSpecifierCase::kGoogleGrpc:
    return std::make_unique<GoogleAsyncClientFactoryImpl>(tls_, google_tls_slot_.get(), scope,
                                                          config, api_, stat_names_);
  case envoy::config::core::v3::GrpcService::TargetSpecifierCase::TARGET_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
  return nullptr;
}

RawAsyncClientSharedPtr AsyncClientManagerImpl::getOrCreateRawAsyncClient(
    const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
    bool skip_cluster_check) {
  const GrpcServiceConfigWithHashKey config_with_hash_key = GrpcServiceConfigWithHashKey(config);
  RawAsyncClientSharedPtr client = raw_async_client_cache_->getCache(config_with_hash_key);
  if (client != nullptr) {
    return client;
  }
  client = factoryForGrpcService(config_with_hash_key.config(), scope, skip_cluster_check)
               ->createUncachedRawAsyncClient();
  raw_async_client_cache_->setCache(config_with_hash_key, client);
  return client;
}

RawAsyncClientSharedPtr AsyncClientManagerImpl::getOrCreateRawAsyncClientWithHashKey(
    const GrpcServiceConfigWithHashKey& config_with_hash_key, Stats::Scope& scope,
    bool skip_cluster_check) {
  RawAsyncClientSharedPtr client = raw_async_client_cache_->getCache(config_with_hash_key);
  if (client != nullptr) {
    return client;
  }
  client = factoryForGrpcService(config_with_hash_key.config(), scope, skip_cluster_check)
               ->createUncachedRawAsyncClient();
  raw_async_client_cache_->setCache(config_with_hash_key, client);
  return client;
}

AsyncClientManagerImpl::RawAsyncClientCache::RawAsyncClientCache(
    Event::Dispatcher& dispatcher, std::chrono::milliseconds max_cached_entry_idle_duration)
    : dispatcher_(dispatcher), max_cached_entry_idle_duration_(max_cached_entry_idle_duration) {
  cache_eviction_timer_ = dispatcher.createTimer([this] { evictEntriesAndResetEvictionTimer(); });
}

void AsyncClientManagerImpl::RawAsyncClientCache::setCache(
    const GrpcServiceConfigWithHashKey& config_with_hash_key,
    const RawAsyncClientSharedPtr& client) {
  ASSERT(lru_map_.find(config_with_hash_key) == lru_map_.end());
  // Create a new cache entry at the beginning of the list.
  lru_list_.emplace_front(config_with_hash_key, client, dispatcher_.timeSource().monotonicTime());
  lru_map_[config_with_hash_key] = lru_list_.begin();
  // If inserting to an empty cache, enable eviction timer.
  if (lru_list_.size() == 1) {
    evictEntriesAndResetEvictionTimer();
  }
}

RawAsyncClientSharedPtr AsyncClientManagerImpl::RawAsyncClientCache::getCache(
    const GrpcServiceConfigWithHashKey& config_with_hash_key) {
  auto it = lru_map_.find(config_with_hash_key);
  if (it == lru_map_.end()) {
    return nullptr;
  }
  const auto cache_entry = it->second;
  // Reset the eviction timer if the next entry to expire is accessed.
  const bool should_reset_timer = (cache_entry == --lru_list_.end());
  cache_entry->accessed_time_ = dispatcher_.timeSource().monotonicTime();
  // Move the cache entry to the beginning of the list upon access.
  lru_list_.splice(lru_list_.begin(), lru_list_, cache_entry);
  // Get the cached async client before any cache eviction.
  RawAsyncClientSharedPtr client = cache_entry->client_;
  if (should_reset_timer) {
    evictEntriesAndResetEvictionTimer();
  }
  return client;
}

void AsyncClientManagerImpl::RawAsyncClientCache::evictEntriesAndResetEvictionTimer() {
  MonotonicTime now = dispatcher_.timeSource().monotonicTime();
  // Evict all the entries that have expired.
  while (!lru_list_.empty()) {
    const MonotonicTime next_expire =
        lru_list_.back().accessed_time_ + max_cached_entry_idle_duration_;
    std::chrono::seconds time_to_next_expire_sec =
        std::chrono::duration_cast<std::chrono::seconds>(next_expire - now);
    // since 'now' and 'next_expire' are in nanoseconds, the following condition is to
    // check if the difference between them is less than 1 second. If we don't do this, the
    // timer will be enabled with 0 seconds, which will cause the timer to fire immediately.
    // This will cause cpu spike.
    if (time_to_next_expire_sec.count() <= 0) {
      // Erase the expired entry.
      lru_map_.erase(lru_list_.back().config_with_hash_key_);
      lru_list_.pop_back();
    } else {
      cache_eviction_timer_->enableTimer(time_to_next_expire_sec);
      return;
    }
  }
}

} // namespace Grpc
} // namespace Envoy
