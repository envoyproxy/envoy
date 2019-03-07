#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

inline envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings
createConnPoolSettings() {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings setting{};
  setting.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(20));
  setting.set_enable_hashtagging(true);
  return setting;
}

} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
