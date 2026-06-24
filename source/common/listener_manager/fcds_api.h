#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/init/manager.h"
#include "envoy/network/filter.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/common/config/resource_type_helper.h"
#include "source/common/init/target_impl.h"

namespace Envoy {
namespace Server {

/**
 * Callbacks for FilterChain updates from FCDS.
 */
class FilterChainUpdateCallbacks {
public:
  virtual ~FilterChainUpdateCallbacks() = default;

  /**
   * Called when filter chains are added, updated or removed.
   * Updates expect the client to start draining the replaced chains.
   */
  virtual absl::Status
  onFilterChainUpdate(const std::vector<Network::DrainableFilterChainSharedPtr>& added_or_updated,
                      const std::vector<Network::DrainableFilterChainSharedPtr>& draining) PURE;
};

class FcdsSharedFilterChainManager;

class FcdsSubscriptionHandle {
public:
  virtual ~FcdsSubscriptionHandle() = default;
};

using FcdsSubscriptionHandlePtr = std::unique_ptr<FcdsSubscriptionHandle>;

/**
 * Interface for FCDS (FilterChain Discovery Service) API.
 */
class FcdsApi : public Config::SubscriptionCallbacks {
public:
  virtual ~FcdsApi() = default;

  /**
   * Start the FCDS subscription.
   */
  virtual void start() PURE;

  /**
   * @return the last received version info from FCDS.
   */
  virtual std::string versionInfo() const PURE;
};

using FcdsApiPtr = std::unique_ptr<FcdsApi>;

/**
 * FCDS API implementation that fetches via Config::Subscription.
 */
class FcdsApiImpl : public FcdsApi, Logger::Loggable<Logger::Id::upstream> {
public:
  FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
                         const std::string& filter_chain_name,
                         FcdsSharedFilterChainManager& shared_manager, 
                         Upstream::ClusterManager& cm,
                         Stats::Scope& scope,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         absl::Status& creation_status);

  ~FcdsApiImpl() override = default;

  // FcdsApi
  void start() override;
  std::string versionInfo() const override { return system_version_info_; }

  // Client Management
  absl::Status subscribeClient(FilterChainUpdateCallbacks& callbacks,
                               Init::TargetImpl& init_target);
  void unsubscribeClient(FilterChainUpdateCallbacks& callbacks);
  bool hasClients() const { return !clients_.empty(); }

  Network::DrainableFilterChainSharedPtr filterChain() const { return filter_chain_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  struct Client {
    FilterChainUpdateCallbacks* callbacks_;
    Init::TargetImpl* init_target_;
    bool init_target_ready_{false};
  };

  const envoy::config::core::v3::ConfigSource fcds_config_;
  const std::string filter_chain_name_;
  FcdsSharedFilterChainManager& shared_manager_;
  Config::SubscriptionPtr subscription_;

  std::vector<Client> clients_;
  std::string system_version_info_;
  Stats::ScopeSharedPtr scope_;
  const Config::ResourceTypeHelper<envoy::config::listener::v3::FilterChain> resource_type_helper_;
  Network::DrainableFilterChainSharedPtr filter_chain_;
  bool started_{false};
};

} // namespace Server
} // namespace Envoy
