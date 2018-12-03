#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"

#include "common/config/grpc_mux_impl.h"
#include "common/config/grpc_mux_subscription_impl.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

template <class ResourceType>
class GrpcManagedMuxSubscriptionImpl : public Config::Subscription<ResourceType> {
public:
  GrpcManagedMuxSubscriptionImpl(GrpcMux& grpc_mux, SubscriptionStats stats)
      : grpc_mux_(grpc_mux), grpc_mux_subscription_(grpc_mux_, stats) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    // Subscribe first, so we get failure callbacks if grpc_mux_.start() fails.
    grpc_mux_subscription_.start(resources, callbacks);
    grpc_mux_.start();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    grpc_mux_subscription_.updateResources(resources);
  }

  GrpcMux& grpcMux() { return grpc_mux_; }

private:
  GrpcMux& grpc_mux_;
  GrpcMuxSubscriptionImpl<ResourceType> grpc_mux_subscription_;
};

} // namespace Config
} // namespace Envoy