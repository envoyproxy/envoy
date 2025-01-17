#pragma once

#include <chrono>
#include <memory>

#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/logger.h"

#include "xds/core/v3/resource_locator.pb.h"

namespace Envoy {
namespace Config {

/**
 * Adapter from typed Subscription to untyped GrpcMux. Also handles per-xDS API stats/logging.
 */
class GrpcSubscriptionImpl : public Subscription,
                             protected SubscriptionCallbacks,
                             Logger::Loggable<Logger::Id::config> {
public:
  GrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux, SubscriptionCallbacks& callbacks,
                       OpaqueResourceDecoderSharedPtr resource_decoder, SubscriptionStats stats,
                       absl::string_view type_url, Event::Dispatcher& dispatcher,
                       std::chrono::milliseconds init_fetch_timeout, bool is_aggregated,
                       const SubscriptionOptions& options);

  // Config::Subscription
  void start(const absl::flat_hash_set<std::string>& resource_names) override;
  void
  updateResourceInterest(const absl::flat_hash_set<std::string>& update_to_these_names) override;
  void requestOnDemandUpdate(const absl::flat_hash_set<std::string>& add_these_names) override;
  // Config::SubscriptionCallbacks (all pass through to callbacks_!)
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override;

  GrpcMuxSharedPtr grpcMux() { return grpc_mux_; }

  ScopedResume pause();

private:
  void disableInitFetchTimeoutTimer();

  GrpcMuxSharedPtr grpc_mux_;
  SubscriptionCallbacks& callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  SubscriptionStats stats_;
  const std::string type_url_;
  GrpcMuxWatchPtr watch_;
  Event::Dispatcher& dispatcher_;
  // NOTE: if another subscription of the same type_url has already been started, this value will be
  // ignored in favor of the other subscription's.
  std::chrono::milliseconds init_fetch_timeout_;
  Event::TimerPtr init_fetch_timeout_timer_;
  const bool is_aggregated_;
  const SubscriptionOptions options_;

  struct ResourceNameFormatter {
    void operator()(std::string* out, const Config::DecodedResourceRef& resource) {
      out->append(resource.get().name());
    }
  };
};

using GrpcSubscriptionImplPtr = std::unique_ptr<GrpcSubscriptionImpl>;
using GrpcSubscriptionImplSharedPtr = std::shared_ptr<GrpcSubscriptionImpl>;

class GrpcCollectionSubscriptionImpl : public GrpcSubscriptionImpl {
public:
  GrpcCollectionSubscriptionImpl(const xds::core::v3::ResourceLocator& collection_locator,
                                 GrpcMuxSharedPtr grpc_mux, SubscriptionCallbacks& callbacks,
                                 OpaqueResourceDecoderSharedPtr resource_decoder,
                                 SubscriptionStats stats, Event::Dispatcher& dispatcher,
                                 std::chrono::milliseconds init_fetch_timeout, bool is_aggregated,
                                 const SubscriptionOptions& options);

  void start(const absl::flat_hash_set<std::string>& resource_names) override;

private:
  xds::core::v3::ResourceLocator collection_locator_;
};

} // namespace Config
} // namespace Envoy
