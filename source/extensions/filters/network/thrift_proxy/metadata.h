#pragma once

#include <algorithm>
#include <cstring>
#include <list>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/common/macros.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"
#include "source/extensions/filters/network/thrift_proxy/tracing.h"

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
  MessageMetadata() = default;

  std::shared_ptr<MessageMetadata> clone() const {
    auto copy = std::make_shared<MessageMetadata>();

    if (hasFrameSize()) {
      copy->setFrameSize(frameSize());
    }

    if (hasProtocol()) {
      copy->setProtocol(protocol());
    }

    if (hasMethodName()) {
      copy->setMethodName(methodName());
    }

    if (hasHeaderFlags()) {
      copy->setHeaderFlags(headerFlags());
    }

    if (hasSequenceId()) {
      copy->setSequenceId(sequenceId());
    }

    if (hasMessageType()) {
      copy->setMessageType(messageType());
    }

    if (hasReplyType()) {
      copy->setReplyType(replyType());
    }

    Http::HeaderMapImpl::copyFrom(copy->headers(), headers());
    copy->mutableSpans().assign(spans().begin(), spans().end());

    if (hasAppException()) {
      copy->setAppException(appExceptionType(), appExceptionMessage());
    }

    copy->setProtocolUpgradeMessage(isProtocolUpgradeMessage());

    auto trace_id = traceId();
    if (trace_id.has_value()) {
      copy->setTraceId(trace_id.value());
    }

    auto trace_id_high = traceIdHigh();
    if (trace_id_high.has_value()) {
      copy->setTraceIdHigh(trace_id_high.value());
    }

    auto span_id = spanId();
    if (span_id.has_value()) {
      copy->setSpanId(span_id.value());
    }

    auto parent_span_id = parentSpanId();
    if (parent_span_id.has_value()) {
      copy->setParentSpanId(parent_span_id.value());
    }

    auto flags_opt = flags();
    if (flags_opt.has_value()) {
      copy->setFlags(flags_opt.value());
    }

    auto sampled_opt = sampled();
    if (sampled_opt.has_value()) {
      copy->setSampled(sampled_opt.value());
    }

    return copy;
  }

  bool hasFrameSize() const { return frame_size_.has_value(); }
  uint32_t frameSize() const { return frame_size_.value(); }
  void setFrameSize(uint32_t size) { frame_size_ = size; }

  bool hasProtocol() const { return proto_.has_value(); }
  ProtocolType protocol() const { return proto_.value(); }
  void setProtocol(ProtocolType proto) { proto_ = proto; }

  bool hasMethodName() const { return method_name_.has_value(); }
  const std::string& methodName() const { return method_name_.value(); }
  void setMethodName(const std::string& method_name) { method_name_ = method_name; }

  bool hasHeaderFlags() const { return header_flags_.has_value(); }
  int16_t headerFlags() const { return header_flags_.value(); }
  void setHeaderFlags(int16_t header_flags) { header_flags_ = header_flags; }

  bool hasSequenceId() const { return seq_id_.has_value(); }
  int32_t sequenceId() const { return seq_id_.value(); }
  void setSequenceId(int32_t seq_id) { seq_id_ = seq_id; }

  bool hasMessageType() const { return msg_type_.has_value(); }
  MessageType messageType() const { return msg_type_.value(); }
  void setMessageType(MessageType msg_type) { msg_type_ = msg_type; }

  bool hasReplyType() const { return reply_type_.has_value(); }
  ReplyType replyType() const { return reply_type_.value(); }
  void setReplyType(ReplyType reply_type) { reply_type_ = reply_type; }

  /**
   * @return HeaderMap of current headers (never throws)
   */
  const Http::HeaderMap& headers() const { return *headers_; }
  Http::HeaderMap& headers() { return *headers_; }

  /**
   * @return SpanList an immutable list of Spans
   */
  const SpanList& spans() const { return spans_; }

  /**
   * @return SpanList& a reference to a mutable list of Spans
   */
  SpanList& mutableSpans() { return spans_; }

  bool hasAppException() const { return app_ex_type_.has_value(); }
  void setAppException(AppExceptionType app_ex_type, const std::string& message) {
    app_ex_type_ = app_ex_type;
    app_ex_msg_ = message;
  }
  AppExceptionType appExceptionType() const { return app_ex_type_.value(); }
  const std::string& appExceptionMessage() const { return app_ex_msg_.value(); }

  bool isProtocolUpgradeMessage() const { return protocol_upgrade_message_; }
  void setProtocolUpgradeMessage(bool upgrade_message) {
    protocol_upgrade_message_ = upgrade_message;
  }

  absl::optional<int64_t> traceId() const { return trace_id_; }
  void setTraceId(int64_t trace_id) { trace_id_ = trace_id; }

  absl::optional<int64_t> traceIdHigh() const { return trace_id_high_; }
  void setTraceIdHigh(int64_t trace_id_high) { trace_id_high_ = trace_id_high; }

  absl::optional<int64_t> spanId() const { return span_id_; }
  void setSpanId(int64_t span_id) { span_id_ = span_id; }

  absl::optional<int64_t> parentSpanId() const { return parent_span_id_; }
  void setParentSpanId(int64_t parent_span_id) { parent_span_id_ = parent_span_id; }

  absl::optional<int64_t> flags() const { return flags_; }
  void setFlags(int64_t flags) { flags_ = flags; }

  absl::optional<bool> sampled() const { return sampled_; }
  void setSampled(bool sampled) { sampled_ = sampled; }

private:
  absl::optional<uint32_t> frame_size_{};
  absl::optional<ProtocolType> proto_{};
  absl::optional<std::string> method_name_{};
  absl::optional<int16_t> header_flags_{};
  absl::optional<int32_t> seq_id_{};
  absl::optional<MessageType> msg_type_{};
  absl::optional<ReplyType> reply_type_{};
  Http::HeaderMapPtr headers_{Http::RequestHeaderMapImpl::create()};
  absl::optional<AppExceptionType> app_ex_type_;
  absl::optional<std::string> app_ex_msg_;
  bool protocol_upgrade_message_{false};
  SpanList spans_;
  absl::optional<int64_t> trace_id_;
  absl::optional<int64_t> trace_id_high_;
  absl::optional<int64_t> span_id_;
  absl::optional<int64_t> parent_span_id_;
  absl::optional<int64_t> flags_;
  absl::optional<bool> sampled_;
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

/**
 * Constant Thrift headers. All lower case.
 */
class HeaderValues {
public:
  const Http::LowerCaseString ClientId{":client-id"};
  const Http::LowerCaseString Dest{":dest"};
  const Http::LowerCaseString MethodName{":method-name"};
};
using Headers = ConstSingleton<HeaderValues>;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
