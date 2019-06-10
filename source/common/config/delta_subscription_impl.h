#pragma once

#include "envoy/config/subscription.h"

#include "common/config/grpc_delta_xds_context.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

/**
 * DeltaSubscription manages the logic of a delta xDS subscription for a particular resource
 * type. It uses a GrpcDeltaXdsContext to handle the actual gRPC communication with the xDS server.
 * DeltaSubscription and GrpcDeltaXdsContext are both used for both ADS and non-aggregated xDS;
 * the only difference is that ADS has multiple DeltaSubscriptions sharing a single
 * GrpcDeltaXdsContext.
 */
// TODO(fredlas) someday this class will be named GrpcSubscriptionImpl
// TODO TODO better comment: DeltaSubscriptionImpl implements the SubscriptionCallbacks interface so
// that it can write to SubscriptionStats upon a config update. The idea is, DeltaSubscriptionImpl
// presents itself as the SubscriptionCallbacks, but actually the *real* SubscriptionCallbacks is
// held by DeltaSubscriptionImpl. DeltaSubscriptionImpl passes through all SubscriptionCallbacks
// calls to the held SubscriptionCallbacks.
class DeltaSubscriptionImpl : public Subscription, public SubscriptionCallbacks {
public:
  DeltaSubscriptionImpl(std::shared_ptr<GrpcMux> context, absl::string_view type_url,
                        SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                        std::chrono::milliseconds init_fetch_timeout);
  ~DeltaSubscriptionImpl();

  void pause();

  void resume();

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResources(const std::set<std::string>& update_to_these_names) override;

  // Config::SubscriptionCallbacks (all pass through to callbacks_!)
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override;

  std::shared_ptr<GrpcMux> getContextForTest() { return context_; }

private:
  std::shared_ptr<GrpcMux> context_;
  const std::string type_url_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  // NOTE: if another subscription of the same type_url has already been started, this value will be
  // ignored in favor of the other subscription's.
  std::chrono::milliseconds init_fetch_timeout_;
  WatchPtr watch_;
};

} // namespace Config
} // namespace Envoy
