#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"

#include <limits>
#include <memory>
#include <string>

#include "source/common/common/logger.h"

#include "absl/log/absl_check.h"
#include "grpc_transcoding/message_reader.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

namespace {
using ::google::grpc::transcoding::kGrpcDelimiterByteSize;
} // namespace

absl::StatusOr<ParsedGrpcMessage> parseGrpcMessage(CreateMessageDataFunc& factory,
                                                   Envoy::Buffer::Instance& request_in) {
  // Parse the gRPC frame header.
  if (request_in.length() < kGrpcDelimiterByteSize) {
    ENVOY_LOG_MISC(info, "Need more data for gRPC frame header parsing. Current size={}",
                   request_in.length());
    ParsedGrpcMessage ret;
    ret.needs_more_data = true;
    return ret;
  }
  unsigned char frame_header[kGrpcDelimiterByteSize];
  request_in.copyOut(0, kGrpcDelimiterByteSize, &frame_header);

  // Parse the message size.
  auto message_size_status = delimiterToSize(frame_header);
  if (!message_size_status.ok()) {
    return message_size_status.status();
  }
  const uint64_t message_size = message_size_status.value();
  const uint64_t total_size = message_size + kGrpcDelimiterByteSize;
  if (request_in.length() < total_size) {
    ENVOY_LOG_MISC(info, "Need more data for gRPC frame. Current size={}, needed={}",
                   request_in.length(), total_size);
    ParsedGrpcMessage ret;
    ret.needs_more_data = true;
    return ret;
  }

  // Drain out the gRPC frame header as we already buffered it separately.
  ParsedGrpcMessage output;
  output.frame_header.move(request_in, kGrpcDelimiterByteSize);

  // Move the required data to its own BEFORE any parsing occurs.
  // Data cannot be moved again after parsing.
  output.owned_bytes = std::make_unique<Envoy::Buffer::OwnedImpl>();
  output.owned_bytes->move(request_in, message_size);

  // Construct the RawMessage for extraction.
  // Get enough data from the buffer for the first gRPC message.
  uint64_t bytes_needed = message_size;
  output.message = factory();
  const Envoy::Buffer::RawSliceVector slices = output.owned_bytes->getRawSlices();
  for (auto slice : slices) {
    uint64_t slice_length = slice.len_;
    const char* slice_mem = static_cast<const char*>(slice.mem_);

    if (slice_length >= bytes_needed) {
      // We have enough data to continue with extraction.
      output.message->AppendExternalMemory(slice_mem, bytes_needed);
      bytes_needed = 0;

      break;
    }

    // Current slice does not have enough bytes.
    // Add entire slice and move to next slice.
    output.message->AppendExternalMemory(static_cast<const char*>(slice_mem), slice_length);
    bytes_needed -= slice_length;
  }

  ABSL_DCHECK(bytes_needed == 0) << "Tried reading past the array of slices during "
                                    "parsing. This should never happen as "
                                    "caller already did size checks.";

  return output;
}

absl::StatusOr<std::string> sizeToDelimiter(uint64_t size) {
  if (size > std::numeric_limits<uint32_t>::max()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Current input message size ", size, " is larger than max allowed gRPC message length of ",
        std::numeric_limits<uint32_t>::max()));
  }

  unsigned char delimiter[kGrpcDelimiterByteSize];
  // Byte 0 is the compression bit - set to 0 (no compression)
  delimiter[0] = 0;
  // Bytes 1-4 are big-endian 32-bit message size
  delimiter[4] = 0xFF & size;
  size >>= 8;
  delimiter[3] = 0xFF & size;
  size >>= 8;
  delimiter[2] = 0xFF & size;
  size >>= 8;
  delimiter[1] = 0xFF & size;

  return std::string(reinterpret_cast<const char*>(delimiter), kGrpcDelimiterByteSize);
}

absl::StatusOr<uint64_t> delimiterToSize(const unsigned char* delimiter) {
  if (delimiter[0] != 0) {
    // We do not support `Compressed-Flag = 1` case.
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported gRPC frame 'Compressed-Flag'", std::to_string(delimiter[0])));
  }

  uint64_t size = 0;
  // Bytes 1-4 are big-endian 32-bit message size
  size = size | static_cast<unsigned>(delimiter[1]);
  size <<= 8;
  size = size | static_cast<unsigned>(delimiter[2]);
  size <<= 8;
  size = size | static_cast<unsigned>(delimiter[3]);
  size <<= 8;
  size = size | static_cast<unsigned>(delimiter[4]);
  return size;
}
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
