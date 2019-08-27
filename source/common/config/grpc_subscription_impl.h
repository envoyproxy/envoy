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

class GrpcSubscriptionImpl : public Config::Subscription {
public:
  GrpcSubscriptionImpl(const LocalInfo::LocalInfo& local_info, Grpc::RawAsyncClientPtr async_client,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const Protobuf::MethodDescriptor& service_method, absl::string_view type_url,
                       SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                       Stats::Scope& scope, const RateLimitSettings& rate_limit_settings,
                       std::chrono::milliseconds init_fetch_timeout, bool skip_subsequent_node)
      : callbacks_(callbacks),
        grpc_mux_(local_info, std::move(async_client), dispatcher, service_method, random, scope,
                  rate_limit_settings, skip_subsequent_node),
        grpc_mux_subscription_(grpc_mux_, callbacks_, stats, type_url, dispatcher,
                               init_fetch_timeout) {}

  // Config::Subscription
  void start(const std::set<std::string>& resource_names) override {
    // Subscribe first, so we get failure callbacks if grpc_mux_.start() fails.
    grpc_mux_subscription_.start(resource_names);
    grpc_mux_.start();
  }

  void updateResources(const std::set<std::string>& update_to_these_names) override {
    grpc_mux_subscription_.updateResources(update_to_these_names);
  }

  GrpcMuxImpl& grpcMux() { return grpc_mux_; }

private:
  Config::SubscriptionCallbacks& callbacks_;
  GrpcMuxImpl grpc_mux_;
  GrpcMuxSubscriptionImpl grpc_mux_subscription_;
};

} // namespace Config
} // namespace Envoy
