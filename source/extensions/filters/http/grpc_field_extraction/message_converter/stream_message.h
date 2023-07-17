#pragma once
#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"

#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

// A stream message object wraps an individual gRPC message for a gRPC stream.
// It is neither copyable nor movable.
class StreamMessage {
public:
  StreamMessage(std::unique_ptr<Protobuf::field_extraction::MessageData> message,
                Buffer::InstancePtr owned_bytes, std::shared_ptr<uint64_t> ref_to_bytes_usage)
      : message_(std::move(message)), owned_bytes_(std::move(owned_bytes)),
        ref_to_bytes_usage_(ref_to_bytes_usage) {
    if (owned_bytes_ != nullptr) {
      ENVOY_LOG_MISC(debug, "owned len(owned_bytes_)={}", owned_bytes_->length());
      *ref_to_bytes_usage_ += owned_bytes_->length();
    }
  }

  StreamMessage(const StreamMessage&) = delete;

  StreamMessage& operator=(const StreamMessage&) = delete;

  ~StreamMessage() {
    if (owned_bytes_ != nullptr) {
      ENVOY_LOG_MISC(debug, "free len(owned_bytes_)={}", owned_bytes_->length());
      *ref_to_bytes_usage_ -= owned_bytes_->length();
    } else {
      ENVOY_LOG_MISC(debug, "free final placeholder message in the stream");
    }
  }

  int64_t size() { return message_ != nullptr ? message_->Size() : -1; }

  void setIsFirstMessage(bool is_first_message) { is_first_message_ = is_first_message; }

  void setIsFinalMessage(bool is_final_message) { is_final_message_ = is_final_message; }

  bool isFirstMessage() { return is_first_message_; }

  bool isFinalMessage() { return is_final_message_; }

  bool is_first_message_ = false;
  bool is_final_message_ = false;

  Protobuf::field_extraction::MessageData* message() {
    return message_ != nullptr ? message_.get() : nullptr;
  }

  void set(std::unique_ptr<Protobuf::field_extraction::MessageData> message) {
    message_ = std::move(message);
  }

private:
  std::unique_ptr<Protobuf::field_extraction::MessageData> message_;

  // Envoy buffer that owns the underlying data.
  // This data can NOT be mutated during the lifetime of this message.
  Buffer::InstancePtr owned_bytes_;

  // A reference to the total number of bytes consumed by these messages.
  const std::shared_ptr<uint64_t> ref_to_bytes_usage_;
};

using StreamMessagePtr = std::unique_ptr<StreamMessage>;
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
