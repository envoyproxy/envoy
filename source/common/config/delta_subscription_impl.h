#pragma once

#include "envoy/config/subscription.h"
#include "envoy/config/grpc_mux.h"

#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

/**
 * DeltaSubscription manages the logic of a delta xDS subscription for a particular resource
 * type. It uses a GrpcDeltaXdsContext to handle the actual gRPC communication with the xDS server.
 * DeltaSubscription and GrpcDeltaXdsContext are both used for both ADS and non-aggregated xDS;
 * the only difference is that ADS has multiple DeltaSubscriptions sharing a single
 * GrpcDeltaXdsContext.
 */
// TODO(fredlas) someday this class will be named SubscriptionImpl (without any changes to its code)
class DeltaSubscriptionImpl : public Subscription {
public:
  DeltaSubscriptionImpl(std::shared_ptr<GrpcMux> context, absl::string_view type_url,
                        SubscriptionStats stats, std::chrono::milliseconds init_fetch_timeout)
      : context_(context), type_url_(type_url), stats_(stats),
        init_fetch_timeout_(init_fetch_timeout) {}

  ~DeltaSubscriptionImpl() {
    context_->removeSubscription(type_url_);
  } // TODO TODO hmmm maybe the watch stuff should be involved...... or...... this itself is the
    // watch? yeah! actually, i think this itself is the watch. with the existing GrpxMux stuff, you
    // would provide callbacks (probably even just a ref to a cb object) that would get called when
    // your subscription had activity, rather than actually owning the subscription. so, you needed
    // the watch thing to have the ability to tell GrpcMux "no stop these updates". now, with the
    // "subscription shared-owns a context" thing, you just destroy the subscription object, and
    // that ends the on-the-wire xDS activity for this sub.

  void pause() { context_->pause(type_url_); }

  void resume() { context_->resume(type_url_); }

  // Config::DeltaSubscription
  void start(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks) override {
    context_->addSubscription(resources, type_url_, callbacks, stats_, init_fetch_timeout_);
  }

  virtual void updateResources(const std::set<std::string>& update_to_these_names) override {
    context_->updateResources(update_to_these_names, type_url_);
  }

private:
  std::shared_ptr<GrpcMux> context_;
  const std::string type_url_;
  SubscriptionStats stats_;
  const std::chrono::milliseconds init_fetch_timeout_;
};

} // namespace Config
} // namespace Envoy
