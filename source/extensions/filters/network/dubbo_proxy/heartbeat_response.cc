#include "extensions/filters/network/dubbo_proxy/heartbeat_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

DubboFilters::DirectResponse::ResponseType
HeartbeatResponse::encode(MessageMetadata& metadata, DubboProxy::Protocol& protocol, Deserializer&,
                          Buffer::Instance& buffer) const {
  ASSERT(metadata.response_status().value() == ResponseStatus::Ok);
  ASSERT(metadata.message_type() == MessageType::Response);
  ASSERT(metadata.is_event());

  const size_t serialized_body_size = 0;
  if (!protocol.encode(buffer, serialized_body_size, metadata)) {
    throw EnvoyException("failed to encode heartbeat message");
  }

  ENVOY_LOG(debug, "buffer length {}", buffer.length());
  return DirectResponse::ResponseType::SuccessReply;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
