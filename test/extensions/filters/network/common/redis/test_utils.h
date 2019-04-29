#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

inline envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings
createConnPoolSettings(int64_t millis = 20, bool hashtagging = true,
                       bool redirection_support = true) {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings setting{};
  setting.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(millis));
  setting.set_enable_hashtagging(hashtagging);
  setting.set_enable_redirection(redirection_support);
  return setting;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
