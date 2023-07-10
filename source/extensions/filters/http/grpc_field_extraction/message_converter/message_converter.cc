#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"

#include <memory>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"

#include "absl/log/absl_check.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "grpc_transcoding/message_reader.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

namespace {
using Envoy::Protobuf::io::ZeroCopyInputStream;
} // namespace

MessageConverter::MessageConverter(std::unique_ptr<CreateMessageDataFunc> factory,
                                   uint32_t buffer_limit)
    : factory_(std::move(factory)), buffer_limit_(buffer_limit),
      parsed_bytes_usage_(std::make_shared<uint64_t>(0)) {}

absl::StatusOr<std::unique_ptr<StreamMessage>>
MessageConverter::AccumulateMessage(Envoy::Buffer::Instance& data, bool end_stream) {
  // Append the input data to buffer that will be parsed.
  parsing_buffer_.move(data);

  ENVOY_LOG_MISC(info, "Checking buffer limits: actual {} > limit {}?", BytesBuffered(),
                 buffer_limit_);
  if (BytesBuffered() > buffer_limit_) {
    return absl::FailedPreconditionError("Rejected because internal buffer limits are exceeded.");
  }

  absl::StatusOr<ParseGrpcMessageOutput> parsed_output =
      ParseGrpcMessage(*factory_, parsing_buffer_);
  if (!parsed_output.ok()) {
    return parsed_output.status();
  }

  bool is_final_message = end_stream && parsing_buffer_.length() == 0;
  if (parsed_output->needs_more_data) {
    if (is_final_message) {
      ENVOY_LOG_MISC(info, "final placeholder message in the stream, not being parsed");
    } else if (end_stream) {
      return absl::InvalidArgumentError("Did not receive enough data for gRPC message parsing.");
    } else {
      ENVOY_LOG_MISC(info, "expecting more data for gRPC message parsing");
      return nullptr;
    }
  }

  auto message_data =
      std::make_unique<StreamMessage>(std::move(parsed_output->message),
                                      std::move(parsed_output->owned_bytes), parsed_bytes_usage_);
  message_data->set_is_first_message(is_first_message_);
  is_first_message_ = false;
  message_data->set_is_final_message(is_final_message);
  conversions_to_message_data_++;

  ENVOY_LOG_MISC(info, "len(parsing_buffer_)={}", parsing_buffer_.length());
  if (parsed_output->owned_bytes != nullptr) {
    ABSL_DCHECK(!parsed_output->needs_more_data);
    ENVOY_LOG_MISC(info, "len(parsed owned_bytes)={}", parsed_output->owned_bytes->length());
  }
  return message_data;
}

absl::StatusOr<std::vector<std::unique_ptr<StreamMessage>>>
MessageConverter::AccumulateMessages(Envoy::Buffer::Instance& data, bool end_stream) {
  std::vector<std::unique_ptr<StreamMessage>> messages;
  while (true) {
    auto message = AccumulateMessage(data, end_stream);
    if (!message.ok()) {
      return message.status();
    }
    if (*message == nullptr) {
      return messages;
    }
    messages.push_back(std::move(*message));
    if (messages.back()->is_final_message()) {
      return messages;
    }
  }
}

absl::StatusOr<Envoy::Buffer::InstancePtr>
MessageConverter::ConvertBackToBuffer(std::unique_ptr<StreamMessage> message) {
  ABSL_DCHECK(message != nullptr);
  conversions_to_envoy_buffer_++;
  if (conversions_to_envoy_buffer_ > conversions_to_message_data_) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Data corruption! Number of conversions to StreamMessage = ", conversions_to_message_data_,
        ", but this is the ", conversions_to_envoy_buffer_,
        " ConvertBackToBuffer call. Perhaps this StreamMessage"
        "belongs to a different MessageConverter?"));
  }

  // Edge case handling: If there is no proto body, nothing to convert.
  Envoy::Buffer::InstancePtr output_message = std::make_unique<Envoy::Buffer::OwnedImpl>();
  if (message->size() == -1) {
    return output_message;
  }

  // Create gRPC frame header and add to output buffer.
  const uint64_t out_message_size = message->size();
  auto delimiter = SizeToDelimiter(out_message_size);
  if (!delimiter.ok()) {
    return delimiter.status();
  }
  output_message->add(*delimiter);

  std::unique_ptr<ZeroCopyInputStream> input_stream =
      message->message()->CreateZeroCopyInputStream();

  // Do NOT let StreamMessage and the current parsed data go out of scope and
  // automatically destruct. Manage destruction lazily via lambda capture.
  auto message_lifetime = std::shared_ptr<StreamMessage>(message.release());
  auto fragment_releasor =
      [message_lifetime](const void*, size_t,
                         const Envoy::Buffer::BufferFragmentImpl* this_fragment) {
        delete this_fragment;
      };

  // Data owned in StreamMessage is appended to Envoy's Buffer via external
  // data reference (no copy).
  const void* data = nullptr;
  int size = 0;
  Envoy::Buffer::BufferFragmentImpl* last_fragment = nullptr;

  while (input_stream->Next(&data, &size)) {
    last_fragment = new Envoy::Buffer::BufferFragmentImpl(data, size, fragment_releasor);
    output_message->addBufferFragment(*last_fragment);
  }

  // Edge case handling: If StreamMessage is empty, then just let it go out of
  // scope and return buffer with only delimiter.
  if (last_fragment == nullptr) {
    ENVOY_LOG_MISC(info, "converted back empty raw_message");
    ABSL_DCHECK_EQ(output_message->length(), google::grpc::transcoding::kGrpcDelimiterByteSize);
    return output_message;
  }

  ENVOY_LOG_MISC(info, "converted back len(raw_message)={}, len(output_message)={}",
                 message_lifetime->size(), output_message->length());
  return output_message;
}

uint64_t MessageConverter::BytesBuffered() const {
  ENVOY_LOG_MISC(info, "{} + {}", parsing_buffer_.length(), *parsed_bytes_usage_);
  return parsing_buffer_.length() + *parsed_bytes_usage_;
}

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
