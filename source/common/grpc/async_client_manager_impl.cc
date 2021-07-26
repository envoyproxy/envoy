#include "source/common/grpc/async_client_manager_impl.h"

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/base64.h"
#include "source/common/grpc/async_client_impl.h"

#include "absl/strings/match.h"

#ifdef ENVOY_GOOGLE_GRPC
#include "source/common/grpc/google_async_client_impl.h"
#endif

namespace Envoy {
namespace Grpc {
namespace {

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

  const std::string& cluster_name = config.envoy_grpc().cluster_name();
  auto all_clusters = cm_.clusters();
  const auto& it = all_clusters.active_clusters_.find(cluster_name);
  if (it == all_clusters.active_clusters_.end()) {
    throw EnvoyException(fmt::format("Unknown gRPC client cluster '{}'", cluster_name));
  }
  if (it->second.get().info()->addedViaApi()) {
    throw EnvoyException(fmt::format("gRPC client cluster '{}' is not static", cluster_name));
  }
}

AsyncClientManagerImpl::AsyncClientManagerImpl(Upstream::ClusterManager& cm,
                                               ThreadLocal::Instance& tls, TimeSource& time_source,
                                               Api::Api& api, const StatNames& stat_names)
    : cm_(cm), tls_(tls), time_source_(time_source), api_(api), stat_names_(stat_names),
      raw_async_client_cache_(tls_) {
  raw_async_client_cache_.set(
      [](Event::Dispatcher&) { return std::make_shared<RawAsyncClientCache>(); });
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
  throw EnvoyException("Google C++ gRPC client is not linked");
#else
  ASSERT(google_tls_slot_ != nullptr);
#endif

  // Check metadata for gRPC API compliance. Uppercase characters are lowered in the HeaderParser.
  // https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
  for (const auto& header : config.initial_metadata()) {
    // Validate key
    if (!validateGrpcHeaderChars(header.key())) {
      throw EnvoyException(
          fmt::format("Illegal characters in gRPC initial metadata header key: {}.", header.key()));
    }

    // Validate value
    // Binary base64 encoded - handled by the GRPC library
    if (!::absl::EndsWith(header.key(), "-bin") &&
        !validateGrpcCompatibleAsciiHeaderValue(header.value())) {
      throw EnvoyException(fmt::format(
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
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return nullptr;
}

RawAsyncClientSharedPtr AsyncClientManagerImpl::getOrCreateRawAsyncClient(
    const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
    bool skip_cluster_check, CacheOption cache_option) {
  if (cache_option == CacheOption::CacheWhenRuntimeEnabled &&
      !Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_grpc_async_client_cache")) {
    return factoryForGrpcService(config, scope, skip_cluster_check)->createUncachedRawAsyncClient();
  }
  RawAsyncClientSharedPtr client = raw_async_client_cache_->getCache(config);
  if (client != nullptr) {
    return client;
  }
  client = factoryForGrpcService(config, scope, skip_cluster_check)->createUncachedRawAsyncClient();
  raw_async_client_cache_->setCache(config, client);
  return client;
}

RawAsyncClientSharedPtr AsyncClientManagerImpl::RawAsyncClientCache::getCache(
    const envoy::config::core::v3::GrpcService& config) const {
  auto it = cache_.find(config);
  if (it == cache_.end()) {
    return nullptr;
  }
  return it->second;
}

void AsyncClientManagerImpl::RawAsyncClientCache::setCache(
    const envoy::config::core::v3::GrpcService& config, const RawAsyncClientSharedPtr& client) {
  cache_[config] = client;
}

} // namespace Grpc
} // namespace Envoy
