#include "source/extensions/filters/network/dubbo_proxy/heartbeat_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DubboFilters::DirectResponse::ResponseType
HeartbeatResponse::encode(MessageMetadata& metadata, DubboProxy::Protocol& protocol,
                          Buffer::Instance& buffer) const {
  ASSERT(metadata.responseStatus() == ResponseStatus::Ok);
  ASSERT(metadata.messageType() == MessageType::HeartbeatResponse);

  if (!protocol.encode(buffer, metadata, "")) {
    throw EnvoyException("failed to encode heartbeat message");
  }

  ENVOY_LOG(debug, "buffer length {}", buffer.length());
  return DirectResponse::ResponseType::SuccessReply;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
