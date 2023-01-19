#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/grpc_mux_impl.h"
#include "source/common/config/grpc_subscription_impl.h"
#include "source/common/config/subscription_factory_impl.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/server/transport_socket_config_impl.h"

/**
 * MultiplexedSubscriptionFactory is used for instantiation of XDS subscriptions so as to minimize
 * the number of open grpc connections used by these subscriptions. This is done by sharing a grpc
 * multiplexer between subscriptions handled per subscription server and xds resource type. Please
 * see https://github.com/envoyproxy/envoy/issues/2943 for additional information and related
 * issues.
 *
 */

namespace Envoy {
namespace Upstream {

class MultiplexedSubscriptionFactory : public Config::SubscriptionFactoryImpl {
public:
  ~MultiplexedSubscriptionFactory() override = default;

  MultiplexedSubscriptionFactory(const LocalInfo::LocalInfo& local_info,
                                 Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                                 ProtobufMessage::ValidationVisitor& validation_visitor,
                                 Api::Api& api, const Server::Instance& server,
                                 Config::XdsResourcesDelegateOptRef xds_resources_delegate,
                                 Config::XdsConfigTrackerOptRef xds_config_tracker);

protected:
  // Config::SubscriptionFactoryImpl
  Config::GrpcMuxSharedPtr
  getOrCreateMux(const envoy::config::core::v3::ApiConfigSource& api_config_source,
                 absl::string_view type_url, Stats::Scope& scope,
                 Config::CustomConfigValidatorsPtr& custom_config_validators) override;

private:
  absl::flat_hash_map<uint64_t, Config::GrpcMuxSharedPtr> muxes_;
  friend class MultiplexedSubscriptionFactoryPeer;
};
} // namespace Upstream
} // namespace Envoy
