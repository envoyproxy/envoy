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

#include "extensions/filters/network/common/redis/supported_commands.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "extensions/filters/network/redis_proxy/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

using Upstreams = std::map<std::string, ConnPool::InstanceSharedPtr>;

class MirrorPolicyImpl : public MirrorPolicy {
public:
  MirrorPolicyImpl(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                       PrefixRoutes::Route::RequestMirrorPolicy&,
                   const ConnPool::InstanceSharedPtr, Runtime::Loader& runtime);

  ConnPool::InstanceSharedPtr upstream() const override { return upstream_; };

  bool shouldMirror(const std::string& command) const override;

private:
  const std::string runtime_key_;
  const absl::optional<envoy::type::v3::FractionalPercent> default_value_;
  const bool exclude_read_commands_;
  ConnPool::InstanceSharedPtr upstream_;
  Runtime::Loader& runtime_;
};

class Prefix : public Route {
public:
  Prefix(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route
             route,
         Upstreams& upstreams, Runtime::Loader& runtime);

  ConnPool::InstanceSharedPtr upstream() const override { return upstream_; }
  const MirrorPolicies& mirrorPolicies() const override { return mirror_policies_; };
  const std::string& prefix() const { return prefix_; }
  bool removePrefix() const { return remove_prefix_; }

private:
  const std::string prefix_;
  const bool remove_prefix_;
  const ConnPool::InstanceSharedPtr upstream_;
  MirrorPolicies mirror_policies_;
};

using PrefixSharedPtr = std::shared_ptr<Prefix>;

class PrefixRoutes : public Router {
public:
  PrefixRoutes(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes&
                   prefix_routes,
               Upstreams&& upstreams, Runtime::Loader& runtime);

  RouteSharedPtr upstreamPool(std::string& key) override;

private:
  TrieLookupTable<PrefixSharedPtr> prefix_lookup_table_;
  const bool case_insensitive_;
  Upstreams upstreams_;
  RouteSharedPtr catch_all_route_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
