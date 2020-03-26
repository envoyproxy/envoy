#include "extensions/filters/http/grpc_json_transcoder/http_body_utils.h"

#include "google/api/httpbody.pb.h"

using Envoy::Protobuf::io::CodedOutputStream;
using Envoy::Protobuf::io::StringOutputStream;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

void HttpBodyUtils::appendHttpBodyEnvelope(
    Buffer::Instance& output, const std::vector<const Protobuf::Field*>& request_body_field_path,
    std::string content_type, uint64_t content_length) {
  // Manually encode the protobuf envelope for the body.
  // See https://developers.google.com/protocol-buffers/docs/encoding#embedded for wire format.

  // Embedded messages are treated the same way as strings (wire type 2).
  constexpr uint32_t ProtobufLengthDelimitedField = 2;

  std::string proto_envelope;
  {
    // For memory safety, the StringOutputStream needs to be destroyed before
    // we read the string.

    const uint32_t http_body_field_number =
        (google::api::HttpBody::kDataFieldNumber << 3) | ProtobufLengthDelimitedField;

    ::google::api::HttpBody body;
    body.set_content_type(std::move(content_type));

    uint64_t envelope_size = body.ByteSizeLong() +
                             CodedOutputStream::VarintSize32(http_body_field_number) +
                             CodedOutputStream::VarintSize64(content_length);
    std::vector<uint32_t> message_sizes;
    message_sizes.reserve(request_body_field_path.size());
    for (auto it = request_body_field_path.rbegin(); it != request_body_field_path.rend(); ++it) {
      const Protobuf::Field* field = *it;
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
      const Protobuf::Field* field = request_body_field_path[i];
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