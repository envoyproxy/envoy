#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "google/api/httpbody.pb.h"
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/response_to_json_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/transcoder_input_stream.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

using RequestTranslator = ::google::grpc::transcoding::ResponseToJsonTranslator;
using RequestTranslateOptions = ::google::grpc::transcoding::JsonResponseTranslateOptions;
using ResponseTranslator = ::google::grpc::transcoding::JsonRequestTranslator;
using ResponseInfo = ::google::grpc::transcoding::RequestInfo;
using ::envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
    GrpcJsonReverseTranscoder;
using ::google::api::HttpRule;
using ::google::grpc::transcoding::Transcoder;
using ::google::grpc::transcoding::TranscoderInputStream;
using ::google::grpc::transcoding::TypeHelper;

GrpcJsonReverseTranscoderConfig::GrpcJsonReverseTranscoderConfig(
    const GrpcJsonReverseTranscoder& transcoder_config, Api::Api& api) {
  Protobuf::FileDescriptorSet descriptor_set;
  if (!transcoder_config.descriptor_path().empty()) {
    auto file_or_error = api.fileSystem().fileReadToEnd(transcoder_config.descriptor_path());
    THROW_IF_NOT_OK(file_or_error.status());
    if (!descriptor_set.ParseFromString(file_or_error.value())) {
      throw EnvoyException("Unable to parse proto descriptor");
    }
  } else if (!transcoder_config.descriptor_binary().empty()) {
    if (!descriptor_set.ParseFromString(transcoder_config.descriptor_binary())) {
      throw EnvoyException("Unable to parse proto descriptor binary");
    }
  } else {
    throw EnvoyException("Descriptor set not set");
  }
  for (auto& file : descriptor_set.file()) {
    if (descriptor_pool_.BuildFile(file) == nullptr) {
      throw EnvoyException("Unable to build proto descriptor pool");
    }
  }

  type_helper_ = std::make_unique<TypeHelper>(Protobuf::util::NewTypeResolverForDescriptorPool(
      Grpc::Common::typeUrlPrefix(), &descriptor_pool_));
  max_request_body_size_ =
      transcoder_config.has_max_request_body_size()
          ? absl::make_optional(transcoder_config.max_request_body_size().value())
          : std::nullopt;
  max_response_body_size_ =
      transcoder_config.has_max_response_body_size()
          ? absl::make_optional(transcoder_config.max_response_body_size().value())
          : std::nullopt;
  api_version_header_ = transcoder_config.api_version_header().empty()
                            ? std::nullopt
                            : absl::make_optional(transcoder_config.api_version_header());
}

const Protobuf::MethodDescriptor*
GrpcJsonReverseTranscoderConfig::GetMethodDescriptor(absl::string_view path) const {
  std::string grpc_method = absl::StrReplaceAll(path.substr(1), {{"/", "."}});
  return descriptor_pool_.FindMethodByName(grpc_method);
}

bool GrpcJsonReverseTranscoderConfig::IsRequestNestedHttpBody(
    const Protobuf::MethodDescriptor* method_descriptor,
    const std::string& http_request_body_field) const {
  std::string request_type_url =
      Grpc::Common::typeUrl(method_descriptor->input_type()->full_name());
  if (http_request_body_field.empty() || http_request_body_field == "*") {
    return false;
  }
  const ProtobufWkt::Type* request_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  std::vector<const ProtobufWkt::Field*> request_body_field_path;
  absl::Status status = type_helper_->ResolveFieldPath(*request_type, http_request_body_field,
                                                       &request_body_field_path);
  if (!status.ok() || request_body_field_path.empty()) {
    ENVOY_LOG(error, "Failed to resolve the request type: {}", request_type_url);
    return false;
  }
  const ProtobufWkt::Type* request_body_type =
      type_helper_->Info()->GetTypeByTypeUrl(request_body_field_path.back()->type_url());

  return request_body_type != nullptr &&
         request_body_type->name() == google::api::HttpBody::descriptor()->full_name();
}

absl::StatusOr<std::unique_ptr<Transcoder>> GrpcJsonReverseTranscoderConfig::CreateTranscoder(
    const Protobuf::MethodDescriptor* method_descriptor, TranscoderInputStream& request_input,
    TranscoderInputStream& response_input) const {
  std::string request_type_url =
      Grpc::Common::typeUrl(method_descriptor->input_type()->full_name());

  RequestTranslateOptions request_translate_options;
  // Setting this to true because we use body field from the google.api.http
  // annotation to create the request payload after the request has been
  // transcoded.
  request_translate_options.json_print_options.preserve_proto_field_names = true;
  // The reverse transcoder doesn't support streaming, setting it to any value
  // will have no effect.
  request_translate_options.stream_newline_delimited = false;
  auto request_translator = std::make_unique<RequestTranslator>(
      type_helper_->Resolver(), request_type_url, false, &request_input, request_translate_options);

  ResponseInfo response_info;
  std::string response_type_url =
      Grpc::Common::typeUrl(method_descriptor->output_type()->full_name());
  response_info.message_type = type_helper_->Info()->GetTypeByTypeUrl(response_type_url);
  if (response_info.message_type == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("Couldn't resolve type: ", method_descriptor->output_type()->full_name()));
  }
  response_info.body_field_path = "*";

  auto response_translator = std::make_unique<ResponseTranslator>(
      type_helper_->Resolver(), &response_input, std::move(response_info), false, true);

  return std::make_unique<TranscoderImpl>(std::move(request_translator),
                                          std::move(response_translator));
}

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
