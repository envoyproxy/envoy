#pragma once

#include "envoy/config/subscription.h"

#include "common/config/new_grpc_mux_impl.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

// GrpcSubscriptionImpl provides a top-level interface to the Envoy's gRPC communication with
// an xDS server, for use by the various xDS users within Envoy. It is built around a (shared)
// GrpcMuxImpl, and the further machinery underlying that. An xDS user indicates interest in
// various resources via start() and updateResourceInterest(). It receives updates to those
// resources via the SubscriptionCallbacks it provides. Multiple users can each have their own
// Subscription object for the same type_url; GrpcMuxImpl maintains a subscription to the
// union of interested resources, and delivers to the users just the resource updates that they
// are "watching" for.
//
// GrpcSubscriptionImpl and GrpcMuxImpl are both built to provide both regular xDS and ADS,
// distinguished by whether multiple GrpcSubscriptionImpls are sharing a single GrpcMuxImpl.
// (Also distinguished by the gRPC method string, but that's taken care of in SubscriptionFactory).
//
// Why does GrpcSubscriptionImpl itself implement the SubscriptionCallbacks interface? So that it
// can write to SubscriptionStats (which needs to live out here in the GrpcSubscriptionImpl) upon a
// config update. GrpcSubscriptionImpl presents itself to WatchMap as the SubscriptionCallbacks,
// and then, after incrementing stats, passes through to the real callbacks_.
class GrpcSubscriptionImpl : public Subscription, public SubscriptionCallbacks {
public:
  // is_aggregated: whether our GrpcMux is also providing ADS to other Subscriptions, or whether
  // it's all ours. The practical difference is that we ourselves must call start() on it only if
  // we are the sole owner.
  GrpcSubscriptionImpl(GrpcMuxSharedPtr grpc_mux, absl::string_view type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                       std::chrono::milliseconds init_fetch_timeout, bool is_aggregated);
  ~GrpcSubscriptionImpl() override;

  void pause();
  void resume();

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override;
  void updateResourceInterest(const std::set<std::string>& update_to_these_names) override;

  // Config::SubscriptionCallbacks (all pass through to callbacks_!)
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  void onConfigUpdateFailed(ConfigUpdateFailureReason reason, const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override;

  GrpcMuxSharedPtr getGrpcMuxForTest() { return grpc_mux_; }

private:
  GrpcMuxSharedPtr const grpc_mux_;
  const std::string type_url_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  // NOTE: if another subscription of the same type_url has already been started, the value of
  // init_fetch_timeout_ will be ignored in favor of the other subscription's.
  const std::chrono::milliseconds init_fetch_timeout_;
  Watch* watch_{};
  const bool is_aggregated_;
};

} // namespace Config
} // namespace Envoy
