#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/router/router.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/message_stream.h"
#include "grpc_transcoding/response_to_json_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/transcoder_input_stream.h"
#include "grpc_transcoding/type_helper.h"

// This filter does the reverse transcoding from gRPC to JSON. It uses the same
// libraries as the grpc_json_transcoder filter, so the request and response
// translators are reversed.
using RequestTranslator = ::google::grpc::transcoding::ResponseToJsonTranslator;
using RequestTranslateOptions = ::google::grpc::transcoding::JsonResponseTranslateOptions;
using ResponseTranslator = ::google::grpc::transcoding::JsonRequestTranslator;
using ::google::grpc::transcoding::MessageStream;
using ::google::grpc::transcoding::Transcoder;
using ::google::grpc::transcoding::TranscoderInputStream;
using ::google::grpc::transcoding::TypeHelper;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

// TranscoderImpl wraps the request and response translators and provides access
// the input and output streams for the translators.
class TranscoderImpl : public Transcoder {
public:
  explicit TranscoderImpl(std::unique_ptr<RequestTranslator> request_translator,
                          std::unique_ptr<ResponseTranslator> response_translator)
      : request_translator_(std::move(request_translator)),
        response_translator_(std::move(response_translator)),
        response_message_stream_(response_translator_->Output()),
        request_stream_(request_translator_->CreateInputStream()),
        response_stream_(response_message_stream_.CreateInputStream()) {}

  TranscoderInputStream* RequestOutput() override { return request_stream_.get(); }

  absl::Status RequestStatus() override { return request_translator_->Status(); }

  Protobuf::io::ZeroCopyInputStream* ResponseOutput() override { return response_stream_.get(); }

  absl::Status ResponseStatus() override { return response_message_stream_.Status(); }

private:
  std::unique_ptr<RequestTranslator> request_translator_;
  std::unique_ptr<ResponseTranslator> response_translator_;
  MessageStream& response_message_stream_;
  std::unique_ptr<TranscoderInputStream> request_stream_;
  std::unique_ptr<TranscoderInputStream> response_stream_;
};

class GrpcJsonReverseTranscoderConfig : public Router::RouteSpecificFilterConfig,
                                        public Envoy::Logger::Loggable<Envoy::Logger::Id::config> {
public:
  GrpcJsonReverseTranscoderConfig(
      const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
          GrpcJsonReverseTranscoder& transcoder_config,
      Api::Api& api);

  // Takes the value of the path header of a gRPC request and returns its path descriptor.
  const Protobuf::MethodDescriptor* GetMethodDescriptor(absl::string_view path) const;

  // Checks if the request body field is of type `google.api.HttpBody`.
  bool IsRequestNestedHttpBody(const Protobuf::MethodDescriptor* method_descriptor,
                               const std::string& request_body_field) const;

  absl::StatusOr<std::unique_ptr<Transcoder>>
  CreateTranscoder(const Protobuf::MethodDescriptor* method_descriptor,
                   TranscoderInputStream& request_input,
                   TranscoderInputStream& response_input) const;

  // Changes the body field to its `json_name` value or to camelCase if the filter
  // is not configured to preserve the proto field names.
  absl::StatusOr<std::string> ChangeBodyFieldName(absl::string_view path,
                                                  absl::string_view body_field) const;

  absl::optional<uint32_t> max_request_body_size_;
  absl::optional<uint32_t> max_response_body_size_;
  absl::optional<std::string> api_version_header_;

private:
  Protobuf::DescriptorPool descriptor_pool_;
  std::unique_ptr<TypeHelper> type_helper_;
  RequestTranslateOptions request_translate_options_;
};

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
