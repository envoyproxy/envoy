#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

inline envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings
createConnPoolSettings(
    int64_t millis = 20, bool hashtagging = true, bool redirection_support = true,
    uint32_t max_unknown_conns = 100,
    envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings::ReadPolicy
        read_policy = envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::
            ConnPoolSettings::MASTER) {
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::ConnPoolSettings setting{};
  setting.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(millis));
  setting.set_enable_hashtagging(hashtagging);
  setting.set_enable_redirection(redirection_support);
  setting.mutable_max_upstream_unknown_connections()->set_value(max_unknown_conns);
  setting.set_read_policy(read_policy);
  return setting;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
