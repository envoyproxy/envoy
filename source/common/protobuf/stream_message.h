#pragma  once
#include "src/message_data/message_data.h"
#include "envoy/buffer/buffer.h"
#include "source/common/common/logger.h"

namespace Envoy::ProtobufMessage {

class StreamMessage {
 public:
  // EnvoyStreamMessage is neither copyable nor movable.
  StreamMessage(
      std::unique_ptr<google::protobuf::field_extraction::MessageData> message,
      std::unique_ptr<Envoy::Buffer::Instance> owned_bytes,
      std::shared_ptr<uint64_t> ref_to_bytes_usage)
      : message_(std::move(message)),
        owned_bytes_(std::move(owned_bytes)),
        ref_to_bytes_usage_(ref_to_bytes_usage) {
    if (owned_bytes_ != nullptr) {
       ENVOY_LOG_MISC(info,"owned len(owned_bytes_)={}" , owned_bytes_->length());
      *ref_to_bytes_usage_ += owned_bytes_->length();
    }
  }

  StreamMessage(const StreamMessage&) = delete;

  StreamMessage& operator=(const StreamMessage&) = delete;

  ~StreamMessage() {
    if (owned_bytes_ != nullptr) {
       ENVOY_LOG_MISC(info,"free len(owned_bytes_)={}" , owned_bytes_->length());
      *ref_to_bytes_usage_ -= owned_bytes_->length();
    } else {
       ENVOY_LOG_MISC(info,"free final placeholder message in the stream");
    }
  }

  int64_t size() { return message_ != nullptr ? message_->Size() : -1; }

  void set_is_first_message(bool is_first_message) {
    is_first_message_ = is_first_message;
  }

  void set_is_final_message(bool is_final_message) {
    is_final_message_ = is_final_message;
  }

  bool is_first_message() { return is_first_message_; }

  bool is_final_message() { return is_final_message_; }

  bool is_first_message_ = false;
  bool is_final_message_ = false;

  google::protobuf::field_extraction::MessageData* message() {
    return message_ != nullptr ? message_.get() : nullptr;
  }

  void set(
      std::unique_ptr<google::protobuf::field_extraction::MessageData> message) {
    message_ = std::move(message);
  }

 private:
  // protected by friend class later.
  std::unique_ptr<google::protobuf::field_extraction::MessageData> message_;

  // Envoy buffer that owns the underlying data.
  // This data can NOT be mutated during the lifetime of this message.
  std::unique_ptr<Envoy::Buffer::Instance> owned_bytes_;

  // A reference to the total number of bytes consumed by these messages.
  const std::shared_ptr<uint64_t> ref_to_bytes_usage_;
};

}  // namespace
