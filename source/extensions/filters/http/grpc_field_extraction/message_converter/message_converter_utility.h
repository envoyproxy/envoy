#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"

#include "absl/status/statusor.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

using CreateMessageDataFunc =
    std::function<std::unique_ptr<Protobuf::field_extraction::MessageData>()>;

// The output for the parseGrpcMessage function.
struct ParsedGrpcMessage {
  // When true, parser needs more data. `request_in` was not changed and no
  // other members below are set.
  bool needs_more_data = false;

  // The bytes in the gRPC frame header that were removed from `request_in`.
  // Guaranteed to only have 5 bytes.
  Envoy::Buffer::OwnedImpl frame_header;

  // The parsed `RawMessage`. Message does not own any of the underlying data.
  // Data is stored in the `owned_bytes` buffer instead.
  std::unique_ptr<Protobuf::field_extraction::MessageData> message;

  // Envoy buffer that owns the underlying data.
  // Remember, data can NOT be moved out of this buffer anytime.
  // This buffer must outlive the `RawMessage`.
  Envoy::Buffer::InstancePtr owned_bytes;
};

// Parses a gRPC data frame into the corresponding proto `RawMessage`.
//
// Parsing occurs from `request_reader` and `request_in`. When a message is
// successfully parsed, the entire gRPC frame will be removed from
// `request_in` and moved to `ParsedGrpcMessage`. Otherwise,
// `request_in` is left un-altered.
//
// Returns OK with message and buffer owning parsed data if parsing succeeds.
// Returns OK with flag if more data is needed.
// Returns error if parsing fails.
absl::StatusOr<ParsedGrpcMessage> parseGrpcMessage(CreateMessageDataFunc& factory,
                                                   Envoy::Buffer::Instance& request_in);

// Creates a gRPC frame header for the given message size.
absl::StatusOr<std::string> sizeToDelimiter(uint64_t size);

// Determines the gRPC message size based on the gRPC frame delimiter.
absl::StatusOr<uint64_t> delimiterToSize(const unsigned char* delimiter);

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
