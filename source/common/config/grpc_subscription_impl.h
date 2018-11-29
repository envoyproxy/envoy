#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client.h"

#include "common/config/grpc_managed_mux_subscription_impl.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/utility.h"

namespace Envoy {
namespace Config {

template <class ResourceType>
class GrpcSubscriptionImpl : public Config::Subscription<ResourceType> {
public:
  GrpcSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const Protobuf::MethodDescriptor& service_method, SubscriptionStats stats,
                       Stats::Scope& scope, const RateLimitSettings& rate_limit_settings)
      : grpc_mux_(local_info, std::move(async_client), dispatcher, service_method, random, scope,
                  rate_limit_settings),
        grpc_managed_mux_subscription_(grpc_mux_, stats) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    grpc_managed_mux_subscription_.start(resources, callbacks);
  }

  void updateResources(const std::vector<std::string>& resources) override {
    grpc_managed_mux_subscription_.updateResources(resources);
  }

  GrpcMuxImpl& grpcMux() { return grpc_mux_; }

private:
  GrpcMuxImpl grpc_mux_;
  GrpcManagedMuxSubscriptionImpl<ResourceType> grpc_managed_mux_subscription_;
};

} // namespace Config
} // namespace Envoy
