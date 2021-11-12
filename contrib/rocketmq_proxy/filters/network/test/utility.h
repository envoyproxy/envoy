#pragma once

#include "contrib/rocketmq_proxy/filters/network/source/config.h"
#include "contrib/rocketmq_proxy/filters/network/source/conn_manager.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class BufferUtility {
public:
  static void fillRequestBuffer(Buffer::OwnedImpl& buffer, RequestCode code);
  static void fillResponseBuffer(Buffer::OwnedImpl& buffer, RequestCode req_code,
                                 ResponseCode resp_code);

  const static std::string topic_name_;
  const static std::string client_id_;
  const static std::string producer_group_;
  const static std::string consumer_group_;
  const static std::string msg_body_;
  const static std::string extra_info_;
  const static int queue_id_;
  static int opaque_;
};
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
