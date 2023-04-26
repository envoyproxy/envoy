#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/options.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_factory.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/health_checker.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/callback_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/outlier_detection_impl.h"
#include "source/common/upstream/resource_manager_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/server/transport_socket_config_impl.h"

namespace Envoy {
namespace Upstream {

class ClusterFactoryContextImpl : public ClusterFactoryContext {
public:
  using LazyCreateDnsResolver = std::function<Network::DnsResolverSharedPtr()>;

  ClusterFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_context,
                            ClusterManager& cluster_manager, Stats::Store& stats,
                            LazyCreateDnsResolver dns_resolver_fn,
                            Ssl::ContextManager& ssl_context_manager,
                            Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api,
                            ProtobufMessage::ValidationVisitor& validation_visitor)
      : stats_(stats), cluster_manager_(cluster_manager), dns_resolver_fn_(dns_resolver_fn),
        ssl_context_manager_(ssl_context_manager),
        outlier_event_logger_(std::move(outlier_event_logger)), added_via_api_(added_via_api),
        validation_visitor_(validation_visitor), server_context_(server_context) {}

  ClusterManager& clusterManager() override { return cluster_manager_; }
  ThreadLocal::SlotAllocator& threadLocal() override { return server_context_.threadLocal(); }
  Runtime::Loader& runtime() override { return server_context_.runtime(); }
  Event::Dispatcher& mainThreadDispatcher() override {
    return server_context_.mainThreadDispatcher();
  }
  AccessLog::AccessLogManager& logManager() override { return server_context_.accessLogManager(); }
  const LocalInfo::LocalInfo& localInfo() const override { return server_context_.localInfo(); }
  const Server::Options& options() override { return server_context_.options(); }
  OptRef<Server::Admin> admin() override { return server_context_.admin(); }
  Api::Api& api() override { return server_context_.api(); }
  Singleton::Manager& singletonManager() override { return server_context_.singletonManager(); }

  Ssl::ContextManager& sslContextManager() override { return ssl_context_manager_; }
  Network::DnsResolverSharedPtr dnsResolver() override {
    if (!dns_resolver_) {
      dns_resolver_ = dns_resolver_fn_();
    }
    return dns_resolver_;
  }
  Stats::Store& stats() override { return stats_; }
  Outlier::EventLoggerSharedPtr outlierEventLogger() override { return outlier_event_logger_; }
  bool addedViaApi() override { return added_via_api_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }

private:
  Stats::Store& stats_;
  ClusterManager& cluster_manager_;
  Network::DnsResolverSharedPtr dns_resolver_;
  LazyCreateDnsResolver dns_resolver_fn_;
  Ssl::ContextManager& ssl_context_manager_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  const bool added_via_api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Server::Configuration::ServerFactoryContext& server_context_;
};

/**
 * Base class for all cluster factory implementation. This class can be directly extended if the
 * custom cluster does not have any custom configuration. For custom cluster with custom
 * configuration, use ConfigurableClusterFactoryBase instead.
 */
class ClusterFactoryImplBase : public ClusterFactory {
public:
  using LazyCreateDnsResolver = std::function<Network::DnsResolverSharedPtr()>;
  /**
   * Static method to get the registered cluster factory and create an instance of cluster.
   */
  static std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  create(Server::Configuration::ServerFactoryContext& server_context,
         const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cluster_manager,
         Stats::Store& stats, LazyCreateDnsResolver dns_resolver_fn,
         Ssl::ContextManager& ssl_context_manager,
         Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api,
         ProtobufMessage::ValidationVisitor& validation_visitor);

  /**
   * Create a dns resolver to be used by the cluster.
   */
  Network::DnsResolverSharedPtr
  selectDnsResolver(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context);

  // Upstream::ClusterFactory
  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  create(Server::Configuration::ServerFactoryContext& server_context,
         const envoy::config::cluster::v3::Cluster& cluster,
         ClusterFactoryContext& context) override;
  std::string name() const override { return name_; }

protected:
  ClusterFactoryImplBase(const std::string& name) : name_(name) {}

private:
  /**
   * Create an instance of ClusterImplBase.
   */
  virtual std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) PURE;
  const std::string name_;
};

/**
 * Common base class for custom cluster factory with custom configuration.
 * @param ConfigProto is the configuration protobuf.
 */
template <class ConfigProto> class ConfigurableClusterFactoryBase : public ClusterFactoryImplBase {
public:
  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() {
    return std::make_unique<ConfigProto>();
  }

protected:
  ConfigurableClusterFactoryBase(const std::string& name) : ClusterFactoryImplBase(name) {}

private:
  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) override {
    ProtobufTypes::MessagePtr config = createEmptyConfigProto();
    Config::Utility::translateOpaqueConfig(cluster.cluster_type().typed_config(),
                                           socket_factory_context.messageValidationVisitor(),
                                           *config);
    return createClusterWithConfig(server_context, cluster,
                                   MessageUtil::downcastAndValidate<const ConfigProto&>(
                                       *config, context.messageValidationVisitor()),
                                   context, socket_factory_context, std::move(stats_scope));
  }

  virtual std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterWithConfig(
      Server::Configuration::ServerFactoryContext& server_context,
      const envoy::config::cluster::v3::Cluster& cluster, const ConfigProto& proto_config,
      ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopeSharedPtr&& stats_scope) PURE;
};

} // namespace Upstream
} // namespace Envoy
