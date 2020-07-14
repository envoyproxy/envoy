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

#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/secret/secret_manager.h"
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

#include "common/common/callback_impl.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/config/metadata.h"
#include "common/config/utility.h"
#include "common/config/well_known_names.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/stats/isolated_store_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/outlier_detection_impl.h"
#include "common/upstream/resource_manager_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Upstream {

class ClusterFactoryContextImpl : public ClusterFactoryContext {

public:
  ClusterFactoryContextImpl(ClusterManager& cluster_manager, Stats::Store& stats,
                            ThreadLocal::SlotAllocator& tls,
                            Network::DnsResolverSharedPtr dns_resolver,
                            Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
                            Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                            AccessLog::AccessLogManager& log_manager,
                            const LocalInfo::LocalInfo& local_info, Server::Admin& admin,
                            Singleton::Manager& singleton_manager,
                            Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api,
                            ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
      : cluster_manager_(cluster_manager), stats_(stats), tls_(tls),
        dns_resolver_(std::move(dns_resolver)), ssl_context_manager_(ssl_context_manager),
        runtime_(runtime), random_(random), dispatcher_(dispatcher), log_manager_(log_manager),
        local_info_(local_info), admin_(admin), singleton_manager_(singleton_manager),
        outlier_event_logger_(std::move(outlier_event_logger)), added_via_api_(added_via_api),
        validation_visitor_(validation_visitor), api_(api) {}

  ClusterManager& clusterManager() override { return cluster_manager_; }
  Stats::Store& stats() override { return stats_; }
  ThreadLocal::SlotAllocator& tls() override { return tls_; }
  Network::DnsResolverSharedPtr dnsResolver() override { return dns_resolver_; }
  Ssl::ContextManager& sslContextManager() override { return ssl_context_manager_; }
  Runtime::Loader& runtime() override { return runtime_; }
  Random::RandomGenerator& random() override { return random_; }
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  AccessLog::AccessLogManager& logManager() override { return log_manager_; }
  const LocalInfo::LocalInfo& localInfo() override { return local_info_; }
  Server::Admin& admin() override { return admin_; }
  Singleton::Manager& singletonManager() override { return singleton_manager_; }
  Outlier::EventLoggerSharedPtr outlierEventLogger() override { return outlier_event_logger_; }
  bool addedViaApi() override { return added_via_api_; }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Api::Api& api() override { return api_; }

private:
  ClusterManager& cluster_manager_;
  Stats::Store& stats_;
  ThreadLocal::SlotAllocator& tls_;
  Network::DnsResolverSharedPtr dns_resolver_;
  Ssl::ContextManager& ssl_context_manager_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;
  AccessLog::AccessLogManager& log_manager_;
  const LocalInfo::LocalInfo& local_info_;
  Server::Admin& admin_;
  Singleton::Manager& singleton_manager_;
  Outlier::EventLoggerSharedPtr outlier_event_logger_;
  const bool added_via_api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

/**
 * Base class for all cluster factory implementation. This class can be directly extended if the
 * custom cluster does not have any custom configuration. For custom cluster with custom
 * configuration, use ConfigurableClusterFactoryBase instead.
 */
class ClusterFactoryImplBase : public ClusterFactory {
public:
  /**
   * Static method to get the registered cluster factory and create an instance of cluster.
   */
  static std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  create(const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cluster_manager,
         Stats::Store& stats, ThreadLocal::Instance& tls,
         Network::DnsResolverSharedPtr dns_resolver, Ssl::ContextManager& ssl_context_manager,
         Runtime::Loader& runtime, Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
         AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
         Server::Admin& admin, Singleton::Manager& singleton_manager,
         Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api,
         ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  /**
   * Create a dns resolver to be used by the cluster.
   */
  Network::DnsResolverSharedPtr
  selectDnsResolver(const envoy::config::cluster::v3::Cluster& cluster,
                    ClusterFactoryContext& context);

  // Upstream::ClusterFactory
  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
  create(const envoy::config::cluster::v3::Cluster& cluster,
         ClusterFactoryContext& context) override;
  std::string name() const override { return name_; }

protected:
  ClusterFactoryImplBase(const std::string& name) : name_(name) {}

private:
  /**
   * Create an instance of ClusterImplBase.
   */
  virtual std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterImpl(
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) PURE;
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
      const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) override {
    ProtobufTypes::MessagePtr config = createEmptyConfigProto();
    Config::Utility::translateOpaqueConfig(
        cluster.cluster_type().typed_config(), ProtobufWkt::Struct::default_instance(),
        socket_factory_context.messageValidationVisitor(), *config);
    return createClusterWithConfig(cluster,
                                   MessageUtil::downcastAndValidate<const ConfigProto&>(
                                       *config, context.messageValidationVisitor()),
                                   context, socket_factory_context, std::move(stats_scope));
  }

  virtual std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster, const ConfigProto& proto_config,
      ClusterFactoryContext& context,
      Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
      Stats::ScopePtr&& stats_scope) PURE;
};

} // namespace Upstream
} // namespace Envoy
