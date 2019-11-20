#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/network/dubbo_proxy/message.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MessageMetadata {
public:
  void setInvocationInfo(RpcInvocationSharedPtr invocation_info) {
    invocation_info_ = invocation_info;
  }
  bool hasInvocationInfo() const { return invocation_info_ != nullptr; }
  const RpcInvocation& invocation_info() const { return *invocation_info_; }

  void setProtocolType(ProtocolType type) { proto_type_ = type; }
  ProtocolType protocol_type() const { return proto_type_; }

  void setProtocolVersion(uint8_t version) { protocol_version_ = version; }
  uint8_t protocol_version() const { return protocol_version_; }

  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType message_type() const { return message_type_; }

  void setRequestId(int64_t id) { request_id_ = id; }
  int64_t request_id() const { return request_id_; }

  void setTimeout(uint32_t timeout) { timeout_ = timeout; }
  absl::optional<uint32_t> timeout() const { return timeout_; }

  void setTwoWayFlag(bool two_way) { is_two_way_ = two_way; }
  bool is_two_way() const { return is_two_way_; }

  template <typename T = SerializationType> void setSerializationType(T type) {
    ASSERT((std::is_same<uint8_t, typename std::underlying_type<T>::type>::value));
    serialization_type_ = static_cast<uint8_t>(type);
  }
  template <typename T = SerializationType> T serialization_type() const {
    ASSERT((std::is_same<uint8_t, typename std::underlying_type<T>::type>::value));
    return static_cast<T>(serialization_type_);
  }

  template <typename T = ResponseStatus> void setResponseStatus(T status) {
    ASSERT((std::is_same<uint8_t, typename std::underlying_type<T>::type>::value));
    response_status_ = static_cast<uint8_t>(status);
  }
  template <typename T = ResponseStatus> T response_status() const {
    ASSERT((std::is_same<uint8_t, typename std::underlying_type<T>::type>::value));
    return static_cast<T>(response_status_.value());
  }
  bool hasResponseStatus() const { return response_status_.has_value(); }

private:
  bool is_two_way_{false};

  MessageType message_type_{MessageType::Request};
  ProtocolType proto_type_{ProtocolType::Dubbo};

  absl::optional<uint8_t> response_status_;
  absl::optional<uint32_t> timeout_;

  RpcInvocationSharedPtr invocation_info_;

  uint8_t serialization_type_{static_cast<uint8_t>(SerializationType::Hessian2)};
  uint8_t protocol_version_{1};
  int64_t request_id_ = 0;
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
