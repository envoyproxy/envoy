#pragma once

#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

struct HeartbeatResponse : public DubboFilters::DirectResponse,
                           Logger::Loggable<Logger::Id::dubbo> {
  HeartbeatResponse() = default;
  ~HeartbeatResponse() override = default;

  using ResponseType = DubboFilters::DirectResponse::ResponseType;
  ResponseType encode(MessageMetadata& metadata, Protocol& protocol,
                      Buffer::Instance& buffer) const override;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
