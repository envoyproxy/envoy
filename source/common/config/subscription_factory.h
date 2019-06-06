#pragma once

#include "envoy/api/api.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Config {

class SubscriptionFactory {
public:
  /**
   * Subscription factory.
   * @param config envoy::api::v2::core::ConfigSource to construct from.
   * @param local_info LocalInfo::LocalInfo local info.
   * @param dispatcher event dispatcher.
   * @param cm cluster manager for async clients (when REST/gRPC).
   * @param random random generator for jittering polling delays (when REST).
   * @param scope stats scope.
   * @param rest_legacy_constructor constructor function for Subscription adapters (when legacy v1
   * REST).
   * @param rest_method fully qualified name of v2 REST API method (as per protobuf service
   *        description).
   * @param grpc_method fully qualified name of v2 gRPC API bidi streaming method (as per protobuf
   *        service description).
   * @param validation_visitor message validation visitor instance.
   * @param api reference to the Api object
   * @param callbacks the callbacks needed by all Subscription objects, to deliver config updates.
   *                  The callbacks must not result in the deletion of the Subscription object.
   */
  static std::unique_ptr<Subscription> subscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& config, const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope, absl::string_view type_url,
      ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
      SubscriptionCallbacks& callbacks);
};

} // namespace Config
} // namespace Envoy
