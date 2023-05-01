#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/redis_proxy/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

// Number of slots to be assigned
static const uint32_t MaxHashSlot = 8192;

using Upstreams = std::map<std::string, ConnPool::InstanceSharedPtr>;

class HashSlot : public Route {
public:
  HashSlot(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes::Route
             route,
         Upstreams& upstreams, Runtime::Loader& runtime);

  ConnPool::InstanceSharedPtr upstream() const override { return upstream_; }
  const MirrorPolicies& mirrorPolicies() const override { return mirror_policies_; };

private:
  const ConnPool::InstanceSharedPtr upstream_;
  MirrorPolicies mirror_policies_;
};

using HashSlotSharedPtr = std::shared_ptr<HashSlot>;

class HashSlotRoutes : public Router {
public:
  HashSlotRoutes(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes&
                   hash_slot_routes,
               Upstreams&& upstreams, Runtime::Loader& runtime);

  RouteSharedPtr upstreamPool(std::string& key) override;

  void setReadFilterCallback(Network::ReadFilterCallbacks* callbacks) override {
    callbacks_ = callbacks;
  }

private:
  std::array<HashSlotSharedPtr, MaxHashSlot> slots_assignment_{};
  Upstreams upstreams_;
  HashSlotSharedPtr catch_all_route_;
  Network::ReadFilterCallbacks* callbacks_{};

  int parseRouteString(std::string& key);
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

