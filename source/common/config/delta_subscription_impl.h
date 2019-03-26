#pragma once

#include "envoy/config/subscription.h"

#include "common/config/grpc_delta_xds_context.h"
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
class DeltaSubscriptionImpl : public Subscription {
public:
  DeltaSubscriptionImpl(
      GrpcDeltaXdsContextSharedPtr context,
      absl::string_view
          type_url, // TODO TODO everwhere should pass this as
                    // Grpc::Common::typeUrl(ResourceType().GetDescriptor()->full_name())
      const LocalInfo::LocalInfo& local_info, std::chrono::milliseconds init_fetch_timeout,
      Event::Dispatcher& dispatcher, SubscriptionStats stats)
      : context_(context), type_url_(type_url), local_info_(local_info),
        init_fetch_timeout_(init_fetch_timeout), dispatcher_(dispatcher), stats_(stats) {}

  ~DeltaSubscriptionImpl() { context_->removeSubscription(type_url_); }

  void pause() { context_->pause(type_url_); }

  void resume() { context_->resume(type_url_); }

  // Config::DeltaSubscription
  void start(const std::vector<std::string>& resources, SubscriptionCallbacks& callbacks) override {
    context_->addSubscription(resources, type_url_, local_info_, callbacks, dispatcher_,
                              init_fetch_timeout_, stats_);
  }

  void updateResources(const std::vector<std::string>& resources) override {
    context_->updateSubscription(resources, type_url_);
  }

private:
  GrpcDeltaXdsContextSharedPtr context_;
  const std::string type_url_;
  const LocalInfo::LocalInfo& local_info_;
  std::chrono::milliseconds init_fetch_timeout_;
  Event::Dispatcher& dispatcher_;
  SubscriptionStats stats_;
};

} // namespace Config
} // namespace Envoy
