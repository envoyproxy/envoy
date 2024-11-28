#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
using ResponseTranslator = ::google::grpc::transcoding::JsonRequestTranslator;
using ::google::grpc::transcoding::MessageStream;
using ::google::grpc::transcoding::Transcoder;
using ::google::grpc::transcoding::TranscoderInputStream;
using ::google::grpc::transcoding::TypeHelper;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

struct HttpRequestParams {
  std::string method;
  std::string http_rule_path;
  std::string http_body_field;
};

struct MethodInfo {
  bool is_request_http_body;
  bool is_response_http_body;
  bool is_request_nested_http_body;
};

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

  absl::Status CreateTranscoder(absl::string_view path, TranscoderInputStream& request_input,
                                TranscoderInputStream& response_input,
                                std::unique_ptr<Transcoder>& transcoder,
                                HttpRequestParams& request_params, MethodInfo& method_info) const;

  absl::optional<uint32_t> max_request_body_size_;
  absl::optional<uint32_t> max_response_body_size_;
  absl::optional<std::string> api_version_header_;

private:
  absl::Status
  ExtractHttpAnnotationValues(const Envoy::Protobuf::MethodDescriptor* method_descriptor,
                              std::string& http_rule_path, std::string& body_field,
                              std::string& method) const;
  Envoy::Protobuf::DescriptorPool descriptor_pool_;
  std::unique_ptr<TypeHelper> type_helper_;
};

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
