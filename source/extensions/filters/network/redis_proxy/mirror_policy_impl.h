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

using Upstreams = std::map<std::string, ConnPool::InstanceSharedPtr>;

class MirrorPolicyImpl : public MirrorPolicy {
public:
  MirrorPolicyImpl(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
                       RequestMirrorPolicy&,
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

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

