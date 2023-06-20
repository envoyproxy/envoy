#pragma once

#include <memory>
#include <string>

#include "source/common/common/assert.h"
#include "source/extensions/common/dubbo/message.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

/**
 * Context of dubbo request/response.
 */
class Context {
public:
  void setSerializeType(SerializeType type) { serialize_type_ = type; }
  SerializeType serializeType() const { return serialize_type_; }

  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType messageType() const { return message_type_; }

  void setResponseStatus(ResponseStatus status) { response_status_ = status; }
  bool hasResponseStatus() const { return response_status_.has_value(); }
  ResponseStatus responseStatus() const {
    ASSERT(hasResponseStatus());
    return response_status_.value();
  }

  // Body size of dubbo request or dubbo response. Only make sense for
  // decoding.
  void setBodySize(size_t size) { body_size_ = size; }
  size_t bodySize() const { return body_size_; }

  void setRequestId(int64_t id) { request_id_ = id; }
  int64_t requestId() const { return request_id_; }

  bool isTwoWay() const { return message_type_ == MessageType::Request; }

  bool heartbeat() const {
    return message_type_ == MessageType::HeartbeatRequest ||
           message_type_ == MessageType::HeartbeatResponse;
  }

private:
  SerializeType serialize_type_{SerializeType::Hessian2};
  MessageType message_type_{MessageType::Request};
  absl::optional<ResponseStatus> response_status_{};

  int64_t request_id_{};

  size_t body_size_{};
};

using ContextPtr = std::unique_ptr<Context>;

class MessageMetadata {
public:
  // Common message context.
  void setContext(ContextPtr context) { message_context_ = std::move(context); }
  bool hasContext() const { return message_context_ != nullptr; }
  const Context& context() const { return *message_context_; }
  Context& mutableContext() { return *message_context_; }

  // Helper methods to access attributes of common context.
  MessageType messageType() const {
    ASSERT(hasContext());
    return message_context_->messageType();
  }
  bool hasResponseStatus() const {
    ASSERT(hasContext());
    return message_context_->hasResponseStatus();
  }
  ResponseStatus responseStatus() const {
    ASSERT(hasContext());
    return message_context_->responseStatus();
  }
  bool heartbeat() const {
    ASSERT(hasContext());
    return message_context_->heartbeat();
  }
  int64_t requestId() const {
    ASSERT(hasContext());
    return message_context_->requestId();
  }

  // Request info.
  void setRequest(RpcRequestPtr request) { rpc_request_ = std::move(request); }
  bool hasRequest() const { return rpc_request_ != nullptr; }
  const RpcRequest& request() const { return *rpc_request_; }
  RpcRequest& mutableRequest() { return *rpc_request_; }

  // Response info.
  void setResponse(RpcResponsePtr response) { rpc_response_ = std::move(response); }
  bool hasResponse() const { return rpc_response_ != nullptr; }
  const RpcResponse& response() const { return *rpc_response_; }
  RpcResponse& mutableResponse() { return *rpc_response_; }

private:
  // Common message context for dubbo request and dubbo response.
  ContextPtr message_context_;

  RpcRequestPtr rpc_request_;

  RpcResponsePtr rpc_response_;
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
