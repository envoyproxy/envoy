#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/to_lower_table.h"

#include "extensions/filters/network/common/multiplexing/conn_pool_impl.h"
#include "extensions/filters/network/common/multiplexing/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Multiplexing {

typedef std::map<std::string, ConnPool::InstanceSharedPtr> Upstreams;

class PrefixRoutes : public Router {
public:
  PrefixRoutes(const envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes&
                   prefix_routes,
               Upstreams&& upstreams);

  ConnPool::InstanceSharedPtr upstreamPool(std::string& key) override;

private:
  struct Prefix {
    const std::string prefix;
    const bool remove_prefix;
    ConnPool::InstanceSharedPtr upstream;
  };

  typedef std::shared_ptr<Prefix> PrefixPtr;

  TrieLookupTable<PrefixPtr> prefix_lookup_table_;
  const ToLowerTable to_lower_table_;
  const bool case_insensitive_;
  Upstreams upstreams_;
  ConnPool::InstanceSharedPtr catch_all_upstream_;
};

} // namespace Multiplexing
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
