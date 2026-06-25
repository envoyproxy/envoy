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
#include "source/common/init/manager_impl.h"
#include "source/common/init/target_impl.h"

namespace Envoy {
namespace Server {

using FilterChainProto = envoy::config::listener::v3::FilterChain;

/**
 * Callbacks for FilterChain updates from FCDS.
 */
class FilterChainUpdateCallbacks {
public:
  virtual ~FilterChainUpdateCallbacks() = default;

  /**
   * Called when a filter chain is updated. Updates expect the client to start draining the replaced
   * chains. Once the filter chain is compiled to a runtime instance, it should call the
   * subscription to update it.
   */
  virtual absl::Status onFilterChainUpdated(const FilterChainProto& proto) PURE;

  /**
   * Called when a filter chain is removed. This should also signal to cleanup the subscription.
   * Updates expect the client to start draining the replaced chains.
   */
  virtual void onFilterChainRemoved(Network::DrainableFilterChainSharedPtr&& draining) PURE;
};

/**
 * Interface for FCDS (FilterChain Discovery Service) API.
 */
class FcdsApi : public Config::SubscriptionCallbacks {
public:
  virtual ~FcdsApi() = default;

  /**
   * A target to start the subscription and block on the first compiled filter chain or failure.
   */
  virtual Init::Target& initTarget() PURE;

  /**
   * Start the FCDS subscription.
   */
  virtual void start() PURE;

  /**
   * Set the filter chain after compilation and mark the target ready.
   */
  virtual void setFilterChain(Network::DrainableFilterChainSharedPtr&& filter_chain) PURE;

  /**
   * Current filter chain.
   */
  virtual Network::DrainableFilterChainSharedPtr filterChain() const PURE;

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
              const std::string& filter_chain_name, FilterChainUpdateCallbacks& callbacks,
              Upstream::ClusterManager& cm, Stats::Scope& scope,
              ProtobufMessage::ValidationVisitor& validation_visitor,
              absl::Status& creation_status);
  ~FcdsApiImpl() override;

  // FcdsApi
  void start() override;
  Init::Target& initTarget() override { return init_target_; }
  std::string versionInfo() const override { return system_version_info_; }
  void setFilterChain(Network::DrainableFilterChainSharedPtr&& filter_chain) override;
  Network::DrainableFilterChainSharedPtr filterChain() const override { return filter_chain_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  const envoy::config::core::v3::ConfigSource fcds_config_;
  const std::string filter_chain_name_;
  FilterChainUpdateCallbacks& callbacks_;
  Stats::ScopeSharedPtr scope_;
  const Config::ResourceTypeHelper<FilterChainProto> resource_type_helper_;
  Init::SharedTargetImpl init_target_;
  Config::SubscriptionPtr subscription_;

  std::optional<FilterChainProto> config_;
  std::string system_version_info_;
  Network::DrainableFilterChainSharedPtr filter_chain_;
  bool started_ : 1 {false};
  bool warming_ : 1 {false};
};

class FcdsFilterChainFactoryContextImpl : public Configuration::FilterChainFactoryContext,
                                          public Network::DrainDecision {
public:
  FcdsFilterChainFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                                    const FilterChainProto& filter_chain);

  // DrainDecision
  bool drainClose(Network::DrainDirection) const override;
  Common::CallbackHandlePtr addOnDrainCloseCb(Network::DrainDirection,
                                              DrainCloseCb) const override {
    IS_ENVOY_BUG("Unexpected function call");
    return nullptr;
  }

  // Configuration::FactoryContext
  Network::DrainDecision& drainDecision() override;
  Init::Manager& initManager() override;
  Stats::Scope& scope() override;
  Stats::Scope& prefixedScope() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Configuration::ServerFactoryContext& serverFactoryContext() override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  bool isQuic() const override;
  bool shouldBypassOverloadManager() const override;

  void startDraining() override { is_draining_.store(true); }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Stats::ScopeSharedPtr scope_;
  Stats::ScopeSharedPtr prefixed_scope_;
  Init::ManagerImpl init_manager_;
  std::atomic<bool> is_draining_{false};
};

} // namespace Server
} // namespace Envoy
