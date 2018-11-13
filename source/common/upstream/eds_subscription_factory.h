#pragma once

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

#include "common/config/grpc_managed_mux_subscription_impl.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

/**
 * EdsSubscriptionFactory is used for instantiation of EDS subscriptions so as to minimize the
 * number of open grpc connections used by thses subscriptions. This is done by sharing a grpc
 * multiplexer between subscriptions handled by the same config server. Please see
 * https://github.com/envoyproxy/envoy/issues/2943 for additional information and related issues.
 *
 * TODO (dmitri-d): This implementation should be generalized to cover RDS.
 */

namespace Envoy {
namespace Upstream {
class EdsSubscriptionFactory {
public:
  std::unique_ptr<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>>
  subscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& config, const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope,
      std::function<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>*()>
          rest_legacy_constructor,
      const std::string& rest_method, const std::string& grpc_method);

protected:
  Config::GrpcMux& getOrCreateMux(const LocalInfo::LocalInfo& local_info,
                                  Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                                  const Protobuf::MethodDescriptor& service_method,
                                  Runtime::RandomGenerator& random,
                                  const ::envoy::api::v2::core::ApiConfigSource& config_source,
                                  Stats::Scope& scope,
                                  const Config::RateLimitSettings& rate_limit_settings);

private:
  std::unordered_map<uint64_t, Config::GrpcMuxPtr> muxes_;
};
} // namespace Upstream
} // namespace Envoy
