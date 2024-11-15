#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/options.h"
#include "envoy/singleton/manager.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"

namespace Envoy {
namespace Upstream {

/**
 * Context passed to cluster factory to access Envoy resources. Cluster factory should only access
 * the rest of the server through this context object.
 */
class ClusterFactoryContext {
public:
  virtual ~ClusterFactoryContext() = default;

  /**
   * @return Server::Configuration::ServerFactoryContext& the server factory context. All the
   *         server-wide resources should be accessed through this context.
   */
  virtual Server::Configuration::ServerFactoryContext& serverFactoryContext() PURE;

  /**
   * @return Upstream::ClusterManager& singleton for use by the entire server.
   * TODO(wbpcode): clusterManager() of ServerFactoryContext still be invalid when loading
   * static cluster. So we need to provide an cluster manager reference here.
   * This could be removed after https://github.com/envoyproxy/envoy/issues/26653 is resolved.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return ProtobufMessage::ValidationVisitor& validation visitor for cluster configuration
   * messages.
   */
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;

  /**
   * @return bool flag indicating whether the cluster is added via api.
   */
  virtual bool addedViaApi() PURE;

  /**
   * @return Network::DnsResolverSharedPtr the dns resolver for the server.
   */
  virtual Network::DnsResolverSharedPtr dnsResolver() PURE;

  /**
   * @return Ssl::ContextManager& the SSL context manager.
   */
  virtual Ssl::ContextManager& sslContextManager() PURE;

  /**
   * @return Outlier::EventLoggerSharedPtr sink for outlier detection event logs.
   */
  virtual Outlier::EventLoggerSharedPtr outlierEventLogger() PURE;
};

/**
 * Implemented by cluster and registered via Registry::registerFactory() or the convenience class
 * RegisterFactory.
 */
class ClusterFactory : public Config::UntypedFactory {
public:
  ~ClusterFactory() override = default;

  /**
   * Create a new instance of cluster. If the implementation is unable to produce a cluster instance
   * with the provided parameters, it should throw an EnvoyException in the case of general error.
   * @param cluster supplies the general protobuf configuration for the cluster.
   * @param context supplies the cluster's context.
   * @return a pair containing the cluster instance as well as an option thread aware load balancer
   * if this cluster has an integrated load balancer or an absl::Satus error on failure.
   */
  virtual absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>
  create(const envoy::config::cluster::v3::Cluster& cluster, ClusterFactoryContext& context) PURE;

  std::string category() const override { return "envoy.clusters"; }
};

} // namespace Upstream
} // namespace Envoy
