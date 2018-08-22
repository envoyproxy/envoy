#pragma once

#include <string.h>

#include <algorithm>
#include <list>
#include <string>

#include "common/common/macros.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/network/thrift_proxy/thrift.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * MessageMetadata encapsulates metadata about Thrift messages. The various fields are considered
 * optional since they may come from either the transport or protocol in some cases. Unless
 * otherwise noted, accessor methods throw absl::bad_optional_access if the corresponding value has
 * not been set.
 */
class MessageMetadata {
public:
  MessageMetadata() {}

  bool hasFrameSize() const { return frame_size_.has_value(); }
  uint32_t frameSize() const { return frame_size_.value(); }
  void setFrameSize(uint32_t size) { frame_size_ = size; }

  bool hasProtocol() const { return proto_.has_value(); }
  ProtocolType protocol() const { return proto_.value(); }
  void setProtocol(ProtocolType proto) { proto_ = proto; }

  bool hasMethodName() const { return method_name_.has_value(); }
  const std::string& methodName() const { return method_name_.value(); }
  void setMethodName(const std::string& method_name) { method_name_ = method_name; }

  bool hasSequenceId() const { return seq_id_.has_value(); }
  int32_t sequenceId() const { return seq_id_.value(); }
  void setSequenceId(int32_t seq_id) { seq_id_ = seq_id; }

  bool hasMessageType() const { return msg_type_.has_value(); }
  MessageType messageType() const { return msg_type_.value(); }
  void setMessageType(MessageType msg_type) { msg_type_ = msg_type; }

  /**
   * @return HeaderMap of current headers (never throws)
   */
  const Http::HeaderMap& headers() const { return headers_; }
  Http::HeaderMap& headers() { return headers_; }

  bool hasAppException() const { return app_ex_type_.has_value(); }
  void setAppException(AppExceptionType app_ex_type, const std::string& message) {
    app_ex_type_ = app_ex_type;
    app_ex_msg_ = message;
  }
  AppExceptionType appExceptionType() const { return app_ex_type_.value(); }
  const std::string& appExceptionMessage() const { return app_ex_msg_.value(); }

private:
  absl::optional<uint32_t> frame_size_{};
  absl::optional<ProtocolType> proto_{};
  absl::optional<std::string> method_name_{};
  absl::optional<int32_t> seq_id_{};
  absl::optional<MessageType> msg_type_{};
  Http::HeaderMapImpl headers_;
  absl::optional<AppExceptionType> app_ex_type_;
  absl::optional<std::string> app_ex_msg_;
};

typedef std::shared_ptr<MessageMetadata> MessageMetadataSharedPtr;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
