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
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
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

class ClusterFactoryContext {

public:
  virtual ~ClusterFactoryContext(){};

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
  virtual ~ClusterFactory(){};

  virtual ClusterSharedPtr create(const envoy::api::v2::Cluster& cluster,
                                  ClusterFactoryContext& context) PURE;

  /**
   * @return std::string the identifying name for a particular implementation of a cluster
   * produced by the factory.
   */
  virtual std::string name() PURE;
};

} // namespace Upstream
} // namespace Envoy
