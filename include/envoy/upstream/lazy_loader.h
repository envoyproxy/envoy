#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/async_client.h"
#include "envoy/http/conn_pool.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/thread_local_cluster.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

/**
 * Lazy loading is only available when using CDS, bootstrap with static clusters will not support
 * lazy loading.
 */
class LazyLoader {
public:
  virtual ~LazyLoader() {}

  virtual void loadCluster(const std::string& cluster) PURE;
};

}
}
