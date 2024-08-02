#include "source/extensions/filters/http/grpc_json_transcoder/http_body_utils.h"

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "google/api/httpbody.pb.h"

using envoy::extensions::filters::http::grpc_json_transcoder::v3::UnknownQueryParams;
using Envoy::Protobuf::io::CodedInputStream;
using Envoy::Protobuf::io::CodedOutputStream;
using Envoy::Protobuf::io::ZeroCopyInputStream;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

namespace {

// Embedded messages are treated the same way as strings (wire type 2).
constexpr uint32_t ProtobufLengthDelimitedField = 2;

bool parseMessageByFieldPath(CodedInputStream* input,
                             absl::Span<const ProtobufWkt::Field* const> field_path,
                             Protobuf::Message* message) {
  if (field_path.empty()) {
    return message->MergeFromCodedStream(input);
  }

  const uint32_t expected_tag = (field_path.front()->number() << 3) | ProtobufLengthDelimitedField;
  for (;;) {
    const uint32_t tag = input->ReadTag();
    if (tag == expected_tag) {
      uint32_t length = 0;
      if (!input->ReadVarint32(&length)) {
        return false;
      }
      auto limit = input->IncrementRecursionDepthAndPushLimit(length);
      if (!parseMessageByFieldPath(input, field_path.subspan(1), message)) {
        return false;
      }
      if (!input->DecrementRecursionDepthAndPopLimit(limit.first)) {
        return false;
      }
    } else if (tag == 0) {
      return true;
    } else {
      if (!Protobuf::internal::WireFormatLite::SkipField(input, tag)) {
        return false;
      }
    }
  }
}
} // namespace

bool HttpBodyUtils::parseMessageByFieldPath(
    ZeroCopyInputStream* stream, const std::vector<const ProtobufWkt::Field*>& field_path,
    Protobuf::Message* message) {
  CodedInputStream input(stream);
  input.SetRecursionLimit(field_path.size());

  return GrpcJsonTranscoder::parseMessageByFieldPath(&input, absl::MakeConstSpan(field_path),
                                                     message);
}

void HttpBodyUtils::appendHttpBodyEnvelope(
    Buffer::Instance& output, const std::vector<const ProtobufWkt::Field*>& request_body_field_path,
    std::string content_type, uint64_t content_length, const UnknownQueryParams& unknown_params) {
  // Manually encode the protobuf envelope for the body.
  // See https://developers.google.com/protocol-buffers/docs/encoding#embedded for wire format.

  std::string proto_envelope;
  {
    // For memory safety, the StringOutputStream needs to be destroyed before
    // we read the string.

    const uint32_t http_body_field_number =
        (google::api::HttpBody::kDataFieldNumber << 3) | ProtobufLengthDelimitedField;

    ::google::api::HttpBody body;
    body.set_content_type(std::move(content_type));
    if (!unknown_params.key().empty()) {
      body.add_extensions()->PackFrom(unknown_params);
    }

    uint64_t envelope_size = body.ByteSizeLong() +
                             CodedOutputStream::VarintSize32(http_body_field_number) +
                             CodedOutputStream::VarintSize64(content_length);
    std::vector<uint32_t> message_sizes;
    message_sizes.reserve(request_body_field_path.size());
    for (auto it = request_body_field_path.rbegin(); it != request_body_field_path.rend(); ++it) {
      const ProtobufWkt::Field* field = *it;
      const uint64_t message_size = envelope_size + content_length;
      const uint32_t field_number = (field->number() << 3) | ProtobufLengthDelimitedField;
      const uint64_t field_size = CodedOutputStream::VarintSize32(field_number) +
                                  CodedOutputStream::VarintSize64(message_size);
      message_sizes.push_back(message_size);
      envelope_size += field_size;
    }
    std::reverse(message_sizes.begin(), message_sizes.end());

    proto_envelope.reserve(envelope_size);

    Envoy::Protobuf::io::StringOutputStream string_stream(&proto_envelope);
    Envoy::Protobuf::io::CodedOutputStream coded_stream(&string_stream);

    // Serialize body field definition manually to avoid the copy of the body.
    for (size_t i = 0; i < request_body_field_path.size(); ++i) {
      const ProtobufWkt::Field* field = request_body_field_path[i];
      const uint32_t field_number = (field->number() << 3) | ProtobufLengthDelimitedField;
      const uint64_t message_size = message_sizes[i];
      coded_stream.WriteTag(field_number);
      coded_stream.WriteVarint64(message_size);
    }
    body.SerializeToCodedStream(&coded_stream);
    coded_stream.WriteTag(http_body_field_number);
    coded_stream.WriteVarint64(content_length);
  }

  output.add(proto_envelope);
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
