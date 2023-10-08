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

#include "source/common/http/header_map_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/network/common/redis/supported_commands.h"
#include "source/extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "source/extensions/filters/network/redis_proxy/router.h"

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

  ConnPool::InstanceSharedPtr upstream(const std::string& command) const override;
  const MirrorPolicies& mirrorPolicies() const override { return mirror_policies_; };
  const std::string& prefix() const { return prefix_; }
  bool removePrefix() const { return remove_prefix_; }
  const std::string& keyFormatter() const { return key_formatter_; }

private:
  const std::string prefix_;
  const std::string key_formatter_;
  const bool remove_prefix_;
  const ConnPool::InstanceSharedPtr upstream_;
  MirrorPolicies mirror_policies_;
  ConnPool::InstanceSharedPtr read_upstream_;
};

using PrefixSharedPtr = std::shared_ptr<Prefix>;

class PrefixRoutes : public Router, public Logger::Loggable<Logger::Id::redis> {
public:
  PrefixRoutes(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes&
                   prefix_routes,
               Upstreams&& upstreams, Runtime::Loader& runtime);

  RouteSharedPtr upstreamPool(std::string& key, const StreamInfo::StreamInfo& stream_info) override;

  /**
   * Formats redis key based on substitution formatter expression that is defined.
   * @param key redis key to be formatted.
   * @param redis_key_formatter substitution formatter expression to format redis key.
   * @param stream_info reference to the stream info used for formatting the key.
   */
  void formatKey(std::string& key, std::string redis_key_formatter,
                 const StreamInfo::StreamInfo& stream_info);

private:
  TrieLookupTable<PrefixSharedPtr> prefix_lookup_table_;
  const bool case_insensitive_;
  Upstreams upstreams_;
  PrefixSharedPtr catch_all_route_;
  const std::string redis_key_formatter_command_ = "%KEY%";
  const std::string redis_key_to_be_replaced_ = "~REPLACED_KEY~";
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
