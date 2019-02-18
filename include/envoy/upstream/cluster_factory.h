#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
//#include "envoy/common/callback.h"
//#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
//#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
//#include "envoy/upstream/load_balancer_type.h"
//#include "envoy/upstream/locality.h"
#include "envoy/upstream/outlier_detection.h"

//
//#include "envoy/api/v2/core/base.pb.h"
//#include "envoy/api/v2/endpoint/endpoint.pb.h"
//#include "envoy/config/typed_metadata.h"
//#include "envoy/event/timer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
//#include "envoy/runtime/runtime.h"
//#include "envoy/secret/secret_manager.h"
//#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/runtime/runtime.h"
#include "envoy/event/dispatcher.h"
#include "envoy/access_log/access_log.h"
#include "envoy/server/admin.h"
#include "envoy/singleton/manager.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/local_info/local_info.h"
#include "envoy/api/api.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/stats/store.h"
//#include "envoy/stats/scope.h"
//#include "envoy/thread_local/thread_local.h"
//#include "envoy/upstream/cluster_manager.h"
//#include "envoy/upstream/health_checker.h"
//#include "envoy/upstream/load_balancer.h"
//#include "envoy/upstream/locality.h"
//#include "envoy/upstream/upstream.h"
//
//#include "common/common/callback_impl.h"
//#include "common/common/enum_to_int.h"
//#include "common/common/logger.h"
//#include "common/config/metadata.h"
//#include "common/config/well_known_names.h"
//#include "common/network/utility.h"
//#include "common/stats/isolated_store_impl.h"
//#include "common/upstream/load_balancer_impl.h"
//#include "common/upstream/outlier_detection_impl.h"
//#include "common/upstream/resource_manager_impl.h"
//
//#include "server/init_manager_impl.h"
//
//#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Upstream {

class ClusterFactoryContext {

public:
  virtual ~ClusterFactoryContext() {};

  virtual ClusterManager& clusterManager() PURE;
  virtual Stats::Store& stats() PURE;
  virtual ThreadLocal::Instance& tls() PURE;
  virtual Network::DnsResolverSharedPtr dnsResolver() PURE;
  virtual Ssl::ContextManager& sslContextManager() PURE;
  virtual Runtime::Loader& runtime() PURE;
  virtual Runtime::RandomGenerator& random() PURE;
  virtual Event::Dispatcher& dispatcher() PURE;
  virtual AccessLog::AccessLogManager& logManager() PURE;
  virtual const LocalInfo::LocalInfo& localInfo() PURE;
  virtual Server::Admin& admin() PURE;
  virtual Singleton::Manager& singletonManager() PURE;
  virtual Outlier::EventLoggerSharedPtr outlierEventLogger() PURE;
  virtual bool addedViaApi() PURE;
  virtual Api::Api& api() PURE;
};

class ClusterFactory {
public:
  virtual ~ClusterFactory() {};

  virtual ClusterSharedPtr create(const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context) PURE;

  /**
 * @return std::string the identifying name for a particular implementation of a cluster
 * produced by the factory.
 */
  virtual std::string name() PURE;
};


} // namespace Upstream
} // namespace Envoy
