#include "source/common/config/grpc_subscription_impl.h"

#include <chrono>

#include "envoy/config/subscription.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

constexpr std::chrono::milliseconds UpdateDurationLogThreshold = std::chrono::milliseconds(50);

GrpcSubscriptionImpl::GrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux,
                                           SubscriptionCallbacks& callbacks,
                                           OpaqueResourceDecoderSharedPtr resource_decoder,
                                           SubscriptionStats stats, absl::string_view type_url,
                                           Event::Dispatcher& dispatcher,
                                           std::chrono::milliseconds init_fetch_timeout,
                                           bool is_aggregated, const SubscriptionOptions& options)
    : grpc_mux_(grpc_mux), callbacks_(callbacks), resource_decoder_(resource_decoder),
      stats_(stats), type_url_(type_url), dispatcher_(dispatcher),
      init_fetch_timeout_(init_fetch_timeout), is_aggregated_(is_aggregated), options_(options) {}

// Config::Subscription
void GrpcSubscriptionImpl::start(const absl::flat_hash_set<std::string>& resources) {
  if (init_fetch_timeout_.count() > 0) {
    init_fetch_timeout_timer_ = dispatcher_.createTimer([this]() -> void {
      onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout, nullptr);
    });
    init_fetch_timeout_timer_->enableTimer(init_fetch_timeout_);
  }

  watch_ = grpc_mux_->addWatch(type_url_, resources, *this, resource_decoder_, options_);

  // The attempt stat here is maintained for the purposes of having consistency between ADS and
  // gRPC/filesystem/REST Subscriptions. Since ADS is push based and muxed, the notion of an
  // "attempt" for a given xDS API combined by ADS is not really that meaningful.
  stats_.update_attempt_.inc();

  // ADS initial request batching relies on the users of the GrpcMux *not* calling start on it,
  // whereas non-ADS xDS users must call it themselves.
  if (!is_aggregated_) {
    grpc_mux_->start();
  }
}

void GrpcSubscriptionImpl::updateResourceInterest(
    const absl::flat_hash_set<std::string>& update_to_these_names) {
  watch_->update(update_to_these_names);
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
  disableInitFetchTimeoutTimer();
  // TODO(mattklein123): In the future if we start tracking per-resource versions, we need to
  // supply those versions to onConfigUpdate() along with the xDS response ("system")
  // version_info. This way, both types of versions can be tracked and exposed for debugging by
  // the configuration update targets.
  auto start = dispatcher_.timeSource().monotonicTime();
  callbacks_.onConfigUpdate(resources, version_info);
  std::chrono::milliseconds update_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      dispatcher_.timeSource().monotonicTime() - start);
  stats_.update_success_.inc();
  stats_.update_attempt_.inc();
  stats_.update_time_.set(DateUtil::nowToMilliseconds(dispatcher_.timeSource()));
  stats_.version_.set(HashUtil::xxHash64(version_info));
  stats_.version_text_.set(version_info);
  stats_.update_duration_.recordValue(update_duration.count());
  ENVOY_LOG(debug, "gRPC config for {} accepted with {} resources with version {}", type_url_,
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
  disableInitFetchTimeoutTimer();
  stats_.update_attempt_.inc();
  auto start = dispatcher_.timeSource().monotonicTime();
  callbacks_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  std::chrono::milliseconds update_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      dispatcher_.timeSource().monotonicTime() - start);
  stats_.update_success_.inc();
  stats_.update_time_.set(DateUtil::nowToMilliseconds(dispatcher_.timeSource()));
  stats_.version_.set(HashUtil::xxHash64(system_version_info));
  stats_.version_text_.set(system_version_info);
  stats_.update_duration_.recordValue(update_duration.count());
}

void GrpcSubscriptionImpl::onConfigUpdateFailed(ConfigUpdateFailureReason reason,
                                                const EnvoyException* e) {
  switch (reason) {
  case Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure:
    stats_.update_failure_.inc();
    ENVOY_LOG(debug, "gRPC update for {} failed", type_url_);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::FetchTimedout:
    stats_.init_fetch_timeout_.inc();
    disableInitFetchTimeoutTimer();
    ENVOY_LOG(warn, "gRPC config: initial fetch timed out for {}", type_url_);
    callbacks_.onConfigUpdateFailed(reason, e);
    break;
  case Envoy::Config::ConfigUpdateFailureReason::UpdateRejected:
    // We expect Envoy exception to be thrown when update is rejected.
    ASSERT(e != nullptr);
    disableInitFetchTimeoutTimer();
    stats_.update_rejected_.inc();
    ENVOY_LOG(warn, "gRPC config for {} rejected: {}", type_url_, e->what());
    callbacks_.onConfigUpdateFailed(reason, e);
    break;
  }

  stats_.update_attempt_.inc();
}

ScopedResume GrpcSubscriptionImpl::pause() { return grpc_mux_->pause(type_url_); }

void GrpcSubscriptionImpl::disableInitFetchTimeoutTimer() {
  if (init_fetch_timeout_timer_) {
    init_fetch_timeout_timer_->disableTimer();
    init_fetch_timeout_timer_.reset();
  }
}

GrpcCollectionSubscriptionImpl::GrpcCollectionSubscriptionImpl(
    const xds::core::v3::ResourceLocator& collection_locator, GrpcMuxSharedPtr grpc_mux,
    SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
    SubscriptionStats stats, Event::Dispatcher& dispatcher,
    std::chrono::milliseconds init_fetch_timeout, bool is_aggregated,
    const SubscriptionOptions& options)
    : GrpcSubscriptionImpl(
          grpc_mux, callbacks, resource_decoder, stats,
          TypeUtil::descriptorFullNameToTypeUrl(collection_locator.resource_type()), dispatcher,
          init_fetch_timeout, is_aggregated, options),
      collection_locator_(collection_locator) {}

void GrpcCollectionSubscriptionImpl::start(const absl::flat_hash_set<std::string>& resource_names) {
  ASSERT(resource_names.empty());
  GrpcSubscriptionImpl::start({XdsResourceIdentifier::encodeUrl(collection_locator_)});
}

} // namespace Config
} // namespace Envoy
