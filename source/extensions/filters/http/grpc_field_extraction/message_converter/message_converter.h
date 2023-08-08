#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"

#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

// MessageConverter is an object that allows conversion from Envoy's gRPC Buffer
// -> StreamMetadata -> Envoy's gRPC Buffer. API usage example:
//
// - Caller has `Envoy::Buffer::Instance` B
// - Caller calls `accumulateMessages(B)`, retrieving converted `StreamMessage`
//   [SM1, SM2, SM3, ...]
// - Caller is expected to call `convertBackToBuffer(SM1)`, then
//   `convertBackToBuffer(SM2)`, etc... to retrieve back Envoy::Buffer
//   [CB1, CB2, CB3, ...]
//
// Note: The caller can run `convertBackToBuffer` in any order. They are allowed
// to convert back buffers out of order.
//
// It is invalid to destruct the `MessageConverter` while any data is in the
// `StreamMessage` form, or while `BytesBuffered` returns a non-zero value.
// Doing so will result in data loss.
//
// MessageConverter is thread-compatible, but it is not tested in multi-threaded
// environments. This class is meant to be used in Envoy's data plane, where
// we will have 1 separate MessageConverter instance per worker thread.
//

class MessageConverter {
public:
  explicit MessageConverter(std::unique_ptr<CreateMessageDataFunc> factory)
      : MessageConverter(std::move(factory), std::numeric_limits<uint32_t>::max()) {}

  MessageConverter(std::unique_ptr<CreateMessageDataFunc> factory, uint32_t buffer_limit);

  // MessageConverter is neither copyable nor movable.
  MessageConverter(const MessageConverter&) = delete;
  MessageConverter& operator=(const MessageConverter&) = delete;

  // Accumulates a gRPC data buffer into a single StreamMessage.
  //
  // Caller can expect all data to be moved out of the input `data` and
  // potentially buffered internally in this `MessageConverter` object.
  //
  // Returns OK with nullptr if only an incomplete message exists
  //         OK with the message, when all fragments of a message were collected
  //         OK with the message and nullptr data, when it is the last message
  //         in a stream
  //         Otherwise, an error indicating the failure to create a message.
  absl::StatusOr<StreamMessagePtr> accumulateMessage(Envoy::Buffer::Instance& data,
                                                     bool end_stream);

  // The same as `AccumulateMessage`, but returns a list of `StreamMessage`.
  // This should be used if the caller supports streaming, as one data buffer
  // maybe have multiple gRPC frames embedded.
  //
  // Caller is expected to call `convertBackToBuffer` in the same order
  // these StreamMessages are returned.
  //
  // Returns OK with `n-1` messages guaranteed to have data. Those messages
  //         were fully parsed. The last message may be a valid message with
  //         nullptr data if it is the last message in the stream.
  //         Otherwise, an error indicating the failure to create a message.
  absl::StatusOr<std::vector<StreamMessagePtr>> accumulateMessages(Envoy::Buffer::Instance& data,
                                                                   bool end_stream);

  // Converts the StreamMessage back into an Envoy data buffer.
  //
  // Ownership of `message` is passed to this function.
  // Once conversion occurs, `message` is no longer valid and must NOT be used.
  //
  // Returns an Envoy Buffer with the same data contents as the `message.`
  // Returns a status if conversion failed.
  absl::StatusOr<Envoy::Buffer::InstancePtr> convertBackToBuffer(StreamMessagePtr message);

  // Returns the total number of bytes stored across all internal buffers.
  // Callers are not expected to explicitly check this, as we internally ensure
  // we do not go over buffer limits. But callers may use this for their own
  // watermarking.
  [[nodiscard]] uint64_t bytesBuffered() const;

private:
  std::unique_ptr<CreateMessageDataFunc> factory_;
  // Internal buffer to move data to before parsing. Useful to wait until
  // Envoy passes us enough gRPC frame data.
  Envoy::Buffer::OwnedImpl parsing_buffer_;

  // Max bytes this converter should store.
  const uint32_t buffer_limit_;

  // Helper to initialize StreamMessage.
  bool is_first_message_ = true;

  // Helper to verify StreamMessages are not double-converted back.
  uint32_t conversions_to_message_data_ = 0;
  uint32_t conversions_to_envoy_buffer_ = 0;

  // Number of bytes buffered in messages that are already parsed, but waiting
  // to be released. Must be a `shared_ptr` because underlying data can outlive
  // this `MessageConverter.
  std::shared_ptr<uint64_t> parsed_bytes_usage_;
};

using MessageConverterPtr = std::unique_ptr<MessageConverter>;

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
