#include "common/config/grpc_subscription_impl.h"

#include <chrono>

#include "common/config/xds_resource.h"
#include "common/protobuf/type_util.h"

namespace Envoy {
namespace Config {

constexpr std::chrono::milliseconds UpdateDurationLogThreshold = std::chrono::milliseconds(50);

GrpcSubscriptionImpl::GrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux, absl::string_view type_url,
                                           SubscriptionCallbacks& callbacks,
                                           OpaqueResourceDecoder& resource_decoder,
                                           SubscriptionStats stats, TimeSource& time_source,
                                           std::chrono::milliseconds init_fetch_timeout,
                                           bool is_aggregated, bool use_namespace_matching)
    : grpc_mux_(std::move(grpc_mux)), type_url_(type_url), callbacks_(callbacks),
      resource_decoder_(resource_decoder), stats_(stats), time_source_(time_source),
      init_fetch_timeout_(init_fetch_timeout), is_aggregated_(is_aggregated),
      use_namespace_matching_(use_namespace_matching) {}

GrpcSubscriptionImpl::~GrpcSubscriptionImpl() {
  if (watch_) {
    grpc_mux_->removeWatch(type_url_, watch_);
  }
}

ScopedResume GrpcSubscriptionImpl::pause() { return grpc_mux_->pause(type_url_); }

// Config::Subscription
void GrpcSubscriptionImpl::start(const absl::flat_hash_set<std::string>& resources) {
  // ADS initial request batching relies on the users of the GrpcMux *not* calling start on it,
  // whereas non-ADS xDS users must call it themselves.
  if (!is_aggregated_) {
    grpc_mux_->start();
  }
  watch_ = grpc_mux_->addWatch(type_url_, resources, *this, resource_decoder_, init_fetch_timeout_,
                               use_namespace_matching_);
  stats_.update_attempt_.inc();
  ENVOY_LOG(debug, "{} subscription started", type_url_);
}

void GrpcSubscriptionImpl::updateResourceInterest(
    const absl::flat_hash_set<std::string>& update_to_these_names) {
  grpc_mux_->updateWatch(type_url_, watch_, update_to_these_names, use_namespace_matching_);
  stats_.update_attempt_.inc();
}

void GrpcSubscriptionImpl::requestOnDemandUpdate(
    const absl::flat_hash_set<std::string>& for_update) {
  grpc_mux_->requestOnDemandUpdate(type_url_, for_update);
  stats_.update_attempt_.inc();
}

// Config::SubscriptionCallbacks
void GrpcSubscriptionImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                          const std::string& version_info) {
  ENVOY_LOG(debug, "{} received SotW update", type_url_);
  stats_.update_attempt_.inc();
  grpc_mux_->disableInitFetchTimeoutTimer();
  auto start = time_source_.monotonicTime();
  callbacks_.onConfigUpdate(resources, version_info);
  std::chrono::milliseconds update_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_source_.monotonicTime() - start);
  stats_.update_success_.inc();
  stats_.update_time_.set(DateUtil::nowToMilliseconds(time_source_));
  stats_.version_.set(HashUtil::xxHash64(version_info));
  stats_.version_text_.set(version_info);
  stats_.update_duration_.recordValue(update_duration.count());
  ENVOY_LOG(debug, "SotW update for {} accepted with {} resources with version {}", type_url_,
            resources.size(), version_info);

  if (update_duration > UpdateDurationLogThreshold) {
    ENVOY_LOG(debug, "gRPC config update took {} ms! Resources names: {}", update_duration.count(),
              absl::StrJoin(resources, ",", ResourceNameFormatter()));
  }
}

void GrpcSubscriptionImpl::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  stats_.update_attempt_.inc();
  grpc_mux_->disableInitFetchTimeoutTimer();
  auto start = time_source_.monotonicTime();
  callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  std::chrono::milliseconds update_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(time_source_.monotonicTime() - start);
  stats_.update_success_.inc();
  stats_.update_time_.set(DateUtil::nowToMilliseconds(time_source_));
  stats_.version_.set(HashUtil::xxHash64(system_version_info));
  stats_.version_text_.set(system_version_info);
  stats_.update_duration_.recordValue(update_duration.count());
}

void GrpcSubscriptionImpl::onConfigUpdateFailed(ConfigUpdateFailureReason reason,
                                                const EnvoyException* e) {
  switch (reason) {
  case Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure:
    // This is a gRPC-stream-level establishment failure, not an xDS-protocol-level failure.
    // So, don't onConfigUpdateFailed() here. Instead, allow a retry of the gRPC stream.
    // If init_fetch_timeout_ is non-zero, the server will continue startup after that timeout.
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "{} update failed: ConnectionFailure", type_url_);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::FetchTimedout:
    stats_.init_fetch_timeout_.inc();
    grpc_mux_->disableInitFetchTimeoutTimer();
    callbacks_.onConfigUpdateFailed(reason, e);
    ENVOY_LOG(debug, "{} update failed: FetchTimedout", type_url_);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::UpdateRejected:
    // We expect Envoy exception to be thrown when update is rejected.
    ASSERT(e != nullptr);
    grpc_mux_->disableInitFetchTimeoutTimer();
    stats_.update_rejected_.inc();
    callbacks_.onConfigUpdateFailed(reason, e);
    ENVOY_LOG(debug, "{} update failed: UpdateRejected", type_url_);
    break;
  }

  stats_.update_attempt_.inc();
}

GrpcCollectionSubscriptionImpl::GrpcCollectionSubscriptionImpl(
    const xds::core::v3::ResourceLocator& collection_locator, GrpcMuxSharedPtr grpc_mux,
    SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
    SubscriptionStats stats, TimeSource& time_source, std::chrono::milliseconds init_fetch_timeout,
    bool is_aggregated)
    : GrpcSubscriptionImpl(
          grpc_mux, TypeUtil::descriptorFullNameToTypeUrl(collection_locator.resource_type()),
          callbacks, resource_decoder, stats, time_source, init_fetch_timeout, is_aggregated,
          false),
      collection_locator_(collection_locator) {}

void GrpcCollectionSubscriptionImpl::start(const absl::flat_hash_set<std::string>& resource_names) {
  ASSERT(resource_names.empty());
  GrpcSubscriptionImpl::start({XdsResourceIdentifier::encodeUrl(collection_locator_)});
}

} // namespace Config
} // namespace Envoy
