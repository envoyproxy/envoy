#pragma once

#include "envoy/config/subscription.h"

#include "common/config/new_grpc_mux_impl.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

// DeltaSubscriptionImpl provides a top-level interface to the Envoy's gRPC communication with
// an xDS server, for use by the various xDS users within Envoy. It is built around a (shared)
// NewGrpcMuxImpl, and the further machinery underlying that. An xDS user indicates interest in
// various resources via start() and updateResourceInterest(). It receives updates to those
// resources via the SubscriptionCallbacks it provides. Multiple users can each have their own
// Subscription object for the same type_url; NewGrpcMuxImpl maintains a subscription to the
// union of interested resources, and delivers to the users just the resource updates that they
// are "watching" for.
//
// DeltaSubscriptionImpl and NewGrpcMuxImpl are both built to provide both regular xDS and ADS,
// distinguished by whether multiple DeltaSubscriptionImpls are sharing a single
// NewGrpcMuxImpl. (And by the gRPC method string, but that's taken care of over in
// SubscriptionFactory).
//
// Why does DeltaSubscriptionImpl itself implement the SubscriptionCallbacks interface? So that it
// can write to SubscriptionStats (which needs to live out here in the DeltaSubscriptionImpl) upon a
// config update. The idea is, DeltaSubscriptionImpl presents itself to WatchMap as the
// SubscriptionCallbacks, and then passes (after incrementing stats) all callbacks through to
// callbacks_, which are the real SubscriptionCallbacks.
class DeltaSubscriptionImpl : public Subscription, public SubscriptionCallbacks {
public:
  // is_aggregated: whether the underlying mux/context is providing ADS to us and others, or whether
  // it's all ours. The practical difference is that we ourselves must call start() on it only in
  // the latter case.
  DeltaSubscriptionImpl(GrpcMuxSharedPtr context, absl::string_view type_url,
                        SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                        std::chrono::milliseconds init_fetch_timeout, bool is_aggregated);
  ~DeltaSubscriptionImpl() override;

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

  GrpcMuxSharedPtr getContextForTest() { return context_; }

private:
  GrpcMuxSharedPtr context_;
  const std::string type_url_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  // NOTE: if another subscription of the same type_url has already been started, this value will be
  // ignored in favor of the other subscription's.
  std::chrono::milliseconds init_fetch_timeout_;
  Watch* watch_{};
  const bool is_aggregated_;
};

} // namespace Config
} // namespace Envoy
