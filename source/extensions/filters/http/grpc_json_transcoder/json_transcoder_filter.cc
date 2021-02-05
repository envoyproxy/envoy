#include "extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

#include <memory>
#include <unordered_set>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/filter.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/grpc_json_transcoder/http_body_utils.h"
#include "extensions/filters/http/well_known_names.h"

#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "google/api/httpbody.pb.h"
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/path_matcher_utility.h"
#include "grpc_transcoding/response_to_json_translator.h"

using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::io::ZeroCopyInputStream;
using Envoy::ProtobufUtil::Status;
using Envoy::ProtobufUtil::error::Code;
using google::api::HttpRule;
using google::grpc::transcoding::JsonRequestTranslator;
using JsonRequestTranslatorPtr = std::unique_ptr<JsonRequestTranslator>;
using google::grpc::transcoding::MessageStream;
using google::grpc::transcoding::PathMatcherBuilder;
using google::grpc::transcoding::PathMatcherUtility;
using google::grpc::transcoding::RequestInfo;
using google::grpc::transcoding::RequestMessageTranslator;
using RequestMessageTranslatorPtr = std::unique_ptr<RequestMessageTranslator>;
using google::grpc::transcoding::ResponseToJsonTranslator;
using ResponseToJsonTranslatorPtr = std::unique_ptr<ResponseToJsonTranslator>;
using google::grpc::transcoding::Transcoder;
using TranscoderPtr = std::unique_ptr<Transcoder>;
using google::grpc::transcoding::TranscoderInputStream;
using TranscoderInputStreamPtr = std::unique_ptr<TranscoderInputStream>;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

struct RcDetailsValues {
  // The gRPC json transcoder filter failed to transcode when processing request headers.
  // This will generally be accompanied by details about the transcoder failure.
  const std::string GrpcTranscodeFailedEarly = "early_grpc_json_transcode_failure";
  // The gRPC json transcoder filter failed to transcode when processing the request body.
  // This will generally be accompanied by details about the transcoder failure.
  const std::string GrpcTranscodeFailed = "grpc_json_transcode_failure";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {

const Http::LowerCaseString& trailerHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "trailer");
}

// Transcoder:
// https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/blob/master/src/include/grpc_transcoding/transcoder.h
// implementation based on JsonRequestTranslator & ResponseToJsonTranslator
class TranscoderImpl : public Transcoder {
public:
  /**
   * Construct a transcoder implementation
   * @param request_translator a JsonRequestTranslator that does the request translation
   * @param response_translator a ResponseToJsonTranslator that does the response translation
   */
  TranscoderImpl(RequestMessageTranslatorPtr request_translator,
                 JsonRequestTranslatorPtr json_request_translator,
                 ResponseToJsonTranslatorPtr response_translator)
      : request_translator_(std::move(request_translator)),
        json_request_translator_(std::move(json_request_translator)),
        request_message_stream_(request_translator_ ? *request_translator_
                                                    : json_request_translator_->Output()),
        response_translator_(std::move(response_translator)),
        request_stream_(request_message_stream_.CreateInputStream()),
        response_stream_(response_translator_->CreateInputStream()) {}

  // Transcoder
  ::google::grpc::transcoding::TranscoderInputStream* RequestOutput() override {
    return request_stream_.get();
  }
  ProtobufUtil::Status RequestStatus() override { return request_message_stream_.Status(); }

  ZeroCopyInputStream* ResponseOutput() override { return response_stream_.get(); }
  ProtobufUtil::Status ResponseStatus() override { return response_translator_->Status(); }

private:
  RequestMessageTranslatorPtr request_translator_;
  JsonRequestTranslatorPtr json_request_translator_;
  MessageStream& request_message_stream_;
  ResponseToJsonTranslatorPtr response_translator_;
  TranscoderInputStreamPtr request_stream_;
  TranscoderInputStreamPtr response_stream_;
};

} // namespace

JsonTranscoderConfig::JsonTranscoderConfig(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    Api::Api& api) {

  disabled_ = proto_config.services().empty();
  if (disabled_) {
    return;
  }

  FileDescriptorSet descriptor_set;

  switch (proto_config.descriptor_set_case()) {
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kProtoDescriptor:
    if (!descriptor_set.ParseFromString(
            api.fileSystem().fileReadToEnd(proto_config.proto_descriptor()))) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kProtoDescriptorBin:
    if (!descriptor_set.ParseFromString(proto_config.proto_descriptor_bin())) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  for (const auto& file : descriptor_set.file()) {
    addFileDescriptor(file);
  }

  convert_grpc_status_ = proto_config.convert_grpc_status();
  if (convert_grpc_status_) {
    addBuiltinSymbolDescriptor("google.protobuf.Any");
    addBuiltinSymbolDescriptor("google.rpc.Status");
  }

  type_helper_ = std::make_unique<google::grpc::transcoding::TypeHelper>(
      Protobuf::util::NewTypeResolverForDescriptorPool(Grpc::Common::typeUrlPrefix(),
                                                       &descriptor_pool_));

  PathMatcherBuilder<MethodInfoSharedPtr> pmb;
  // clang-format off
  // We cannot convert this to a absl hash set as PathMatcherUtility::RegisterByHttpRule takes a
  // std::unordered_set as an argument
  std::unordered_set<std::string> ignored_query_parameters;
  // clang-format on
  for (const auto& query_param : proto_config.ignored_query_parameters()) {
    ignored_query_parameters.insert(query_param);
  }

  for (const auto& service_name : proto_config.services()) {
    auto service = descriptor_pool_.FindServiceByName(service_name);
    if (service == nullptr) {
      throw EnvoyException("transcoding_filter: Could not find '" + service_name +
                           "' in the proto descriptor");
    }
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);

      HttpRule http_rule;
      if (method->options().HasExtension(google::api::http)) {
        http_rule = method->options().GetExtension(google::api::http);
      } else if (proto_config.auto_mapping()) {
        auto post = "/" + service->full_name() + "/" + method->name();
        http_rule.set_post(post);
        http_rule.set_body("*");
      }

      MethodInfoSharedPtr method_info;
      Status status = createMethodInfo(method, http_rule, method_info);
      if (!status.ok()) {
        throw EnvoyException("transcoding_filter: Cannot register '" + method->full_name() +
                             "': " + status.message().ToString());
      }

      if (!PathMatcherUtility::RegisterByHttpRule(pmb, http_rule, ignored_query_parameters,
                                                  method_info)) {
        throw EnvoyException("transcoding_filter: Cannot register '" + method->full_name() +
                             "' to path matcher");
      }
    }
  }

  switch (proto_config.url_unescape_spec()) {
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      ALL_CHARACTERS_EXCEPT_RESERVED:
    pmb.SetUrlUnescapeSpec(
        google::grpc::transcoding::UrlUnescapeSpec::kAllCharactersExceptReserved);
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      ALL_CHARACTERS_EXCEPT_SLASH:
    pmb.SetUrlUnescapeSpec(google::grpc::transcoding::UrlUnescapeSpec::kAllCharactersExceptSlash);
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      ALL_CHARACTERS:
    pmb.SetUrlUnescapeSpec(google::grpc::transcoding::UrlUnescapeSpec::kAllCharacters);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  path_matcher_ = pmb.Build();

  const auto& print_config = proto_config.print_options();
  print_options_.add_whitespace = print_config.add_whitespace();
  print_options_.always_print_primitive_fields = print_config.always_print_primitive_fields();
  print_options_.always_print_enums_as_ints = print_config.always_print_enums_as_ints();
  print_options_.preserve_proto_field_names = print_config.preserve_proto_field_names();

  match_incoming_request_route_ = proto_config.match_incoming_request_route();
  ignore_unknown_query_parameters_ = proto_config.ignore_unknown_query_parameters();
  strict_http_request_validation_ = proto_config.strict_http_request_validation();
}

void JsonTranscoderConfig::addFileDescriptor(const Protobuf::FileDescriptorProto& file) {
  if (descriptor_pool_.BuildFile(file) == nullptr) {
    throw EnvoyException("transcoding_filter: Unable to build proto descriptor pool");
  }
}

void JsonTranscoderConfig::addBuiltinSymbolDescriptor(const std::string& symbol_name) {
  if (descriptor_pool_.FindFileContainingSymbol(symbol_name) != nullptr) {
    return;
  }

  auto* builtin_pool = Protobuf::DescriptorPool::generated_pool();
  if (!builtin_pool) {
    return;
  }

  Protobuf::DescriptorPoolDatabase pool_database(*builtin_pool);
  Protobuf::FileDescriptorProto file_proto;
  pool_database.FindFileContainingSymbol(symbol_name, &file_proto);
  addFileDescriptor(file_proto);
}

Status JsonTranscoderConfig::resolveField(const Protobuf::Descriptor* descriptor,
                                          const std::string& field_path_str,
                                          std::vector<const Protobuf::Field*>* field_path,
                                          bool* is_http_body) {
  const Protobuf::Type* message_type =
      type_helper_->Info()->GetTypeByTypeUrl(Grpc::Common::typeUrl(descriptor->full_name()));
  if (message_type == nullptr) {
    return ProtobufUtil::Status(Code::NOT_FOUND,
                                "Could not resolve type: " + descriptor->full_name());
  }

  Status status = type_helper_->ResolveFieldPath(
      *message_type, field_path_str == "*" ? "" : field_path_str, field_path);
  if (!status.ok()) {
    return status;
  }

  if (field_path->empty()) {
    *is_http_body = descriptor->full_name() == google::api::HttpBody::descriptor()->full_name();
  } else {
    const Protobuf::Type* body_type =
        type_helper_->Info()->GetTypeByTypeUrl(field_path->back()->type_url());
    *is_http_body = body_type != nullptr &&
                    body_type->name() == google::api::HttpBody::descriptor()->full_name();
  }
  return Status::OK;
}

Status JsonTranscoderConfig::createMethodInfo(const Protobuf::MethodDescriptor* descriptor,
                                              const HttpRule& http_rule,
                                              MethodInfoSharedPtr& method_info) {
  method_info = std::make_shared<MethodInfo>();
  method_info->descriptor_ = descriptor;

  Status status =
      resolveField(descriptor->input_type(), http_rule.body(),
                   &method_info->request_body_field_path, &method_info->request_type_is_http_body_);
  if (!status.ok()) {
    return status;
  }

  status = resolveField(descriptor->output_type(), http_rule.response_body(),
                        &method_info->response_body_field_path,
                        &method_info->response_type_is_http_body_);
  if (!status.ok()) {
    return status;
  }

  if (!method_info->response_body_field_path.empty() && !method_info->response_type_is_http_body_) {
    // TODO(euroelessar): Implement https://github.com/envoyproxy/envoy/issues/11136.
    return Status(Code::UNIMPLEMENTED,
                  "Setting \"response_body\" is not supported yet for non-HttpBody fields: " +
                      descriptor->full_name());
  }

  return Status::OK;
}

bool JsonTranscoderConfig::matchIncomingRequestInfo() const {
  return match_incoming_request_route_;
}

bool JsonTranscoderConfig::convertGrpcStatus() const { return convert_grpc_status_; }

ProtobufUtil::Status JsonTranscoderConfig::createTranscoder(
    const Http::RequestHeaderMap& headers, ZeroCopyInputStream& request_input,
    google::grpc::transcoding::TranscoderInputStream& response_input,
    std::unique_ptr<Transcoder>& transcoder, MethodInfoSharedPtr& method_info) const {

  ASSERT(!disabled_);
  const std::string method(headers.getMethodValue());
  std::string path(headers.getPathValue());
  std::string args;

  const size_t pos = path.find('?');
  if (pos != std::string::npos) {
    args = path.substr(pos + 1);
    path = path.substr(0, pos);
  }

  struct RequestInfo request_info;
  std::vector<VariableBinding> variable_bindings;
  method_info =
      path_matcher_->Lookup(method, path, args, &variable_bindings, &request_info.body_field_path);
  if (!method_info) {
    return ProtobufUtil::Status(Code::NOT_FOUND, "Could not resolve " + path + " to a method.");
  }

  auto status = methodToRequestInfo(method_info, &request_info);
  if (!status.ok()) {
    return status;
  }

  for (const auto& binding : variable_bindings) {
    google::grpc::transcoding::RequestWeaver::BindingInfo resolved_binding;
    status = type_helper_->ResolveFieldPath(*request_info.message_type, binding.field_path,
                                            &resolved_binding.field_path);
    if (!status.ok()) {
      if (ignore_unknown_query_parameters_) {
        continue;
      }
      return status;
    }

    // HttpBody fields should be passed as-is and not be parsed as JSON.
    const bool is_http_body = method_info->request_type_is_http_body_;
    const bool is_inside_http_body =
        is_http_body && absl::c_equal(absl::MakeSpan(resolved_binding.field_path)
                                          .subspan(0, method_info->request_body_field_path.size()),
                                      method_info->request_body_field_path);
    if (!is_inside_http_body) {
      resolved_binding.value = binding.value;
      request_info.variable_bindings.emplace_back(std::move(resolved_binding));
    }
  }

  RequestMessageTranslatorPtr request_translator;
  JsonRequestTranslatorPtr json_request_translator;
  if (method_info->request_type_is_http_body_) {
    request_translator = std::make_unique<RequestMessageTranslator>(*type_helper_->Resolver(),
                                                                    false, std::move(request_info));
    request_translator->Input().StartObject(nullptr)->EndObject();
  } else {
    json_request_translator = std::make_unique<JsonRequestTranslator>(
        type_helper_->Resolver(), &request_input, std::move(request_info),
        method_info->descriptor_->client_streaming(), true);
  }

  const auto response_type_url =
      Grpc::Common::typeUrl(method_info->descriptor_->output_type()->full_name());
  ResponseToJsonTranslatorPtr response_translator{new ResponseToJsonTranslator(
      type_helper_->Resolver(), response_type_url, method_info->descriptor_->server_streaming(),
      &response_input, print_options_)};

  transcoder = std::make_unique<TranscoderImpl>(std::move(request_translator),
                                                std::move(json_request_translator),
                                                std::move(response_translator));
  return ProtobufUtil::Status();
}

ProtobufUtil::Status
JsonTranscoderConfig::methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                          google::grpc::transcoding::RequestInfo* info) const {
  const std::string& request_type_full_name = method_info->descriptor_->input_type()->full_name();
  auto request_type_url = Grpc::Common::typeUrl(request_type_full_name);
  info->message_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  if (info->message_type == nullptr) {
    ENVOY_LOG(debug, "Cannot resolve input-type: {}", request_type_full_name);
    return ProtobufUtil::Status(Code::NOT_FOUND,
                                "Could not resolve type: " + request_type_full_name);
  }

  return ProtobufUtil::Status();
}

ProtobufUtil::Status
JsonTranscoderConfig::translateProtoMessageToJson(const Protobuf::Message& message,
                                                  std::string* json_out) const {
  return ProtobufUtil::BinaryToJsonString(
      type_helper_->Resolver(), Grpc::Common::typeUrl(message.GetDescriptor()->full_name()),
      message.SerializeAsString(), json_out, print_options_);
}

JsonTranscoderFilter::JsonTranscoderFilter(JsonTranscoderConfig& config) : config_(config) {}

void JsonTranscoderFilter::initPerRouteConfig() {
  if (!decoder_callbacks_->route() || !decoder_callbacks_->route()->routeEntry()) {
    per_route_config_ = &config_;
    return;
  }

  const std::string& name = HttpFilterNames::get().GrpcJsonTranscoder;
  const auto* entry = decoder_callbacks_->route()->routeEntry();
  const auto* route_local = entry->mostSpecificPerFilterConfigTyped<JsonTranscoderConfig>(name);

  per_route_config_ = route_local ? route_local : &config_;
}

Http::FilterHeadersStatus JsonTranscoderFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  initPerRouteConfig();
  if (per_route_config_->disabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (Grpc::Common::isGrpcRequestHeaders(headers)) {
    ENVOY_LOG(debug, "Request headers has application/grpc content-type. Request is passed through "
                     "without transcoding.");
    return Http::FilterHeadersStatus::Continue;
  }

  const auto status =
      per_route_config_->createTranscoder(headers, request_in_, response_in_, transcoder_, method_);
  if (!status.ok()) {
    ENVOY_LOG(debug, "Failed to transcode request headers: {}", status.error_message());

    if (config_.strict_http_request_validation_) {
      ENVOY_LOG(debug, "Request is rejected due to strict rejection policy.");
      error_ = true;
      decoder_callbacks_->sendLocalReply(
          Http::Code::BadRequest, absl::StrCat("Bad request: ", status.error_message().ToString()),
          nullptr, absl::nullopt,
          absl::StrCat(RcDetails::get().GrpcTranscodeFailedEarly, "{BAD_REQUEST}"));
      return Http::FilterHeadersStatus::StopIteration;
    } else {
      ENVOY_LOG(debug, "Request is passed through without transcoding.");
      return Http::FilterHeadersStatus::Continue;
    }
  }

  if (method_->request_type_is_http_body_) {
    if (headers.ContentType() != nullptr) {
      absl::string_view content_type = headers.getContentTypeValue();
      content_type_.assign(content_type.begin(), content_type.end());
    }

    bool done = !readToBuffer(*transcoder_->RequestOutput(), initial_request_data_);
    if (!done) {
      ENVOY_LOG(
          debug,
          "Transcoding of query arguments of HttpBody request is not done (unexpected state)");
      error_ = true;
      decoder_callbacks_->sendLocalReply(
          Http::Code::BadRequest, "Bad request", nullptr, absl::nullopt,
          absl::StrCat(RcDetails::get().GrpcTranscodeFailedEarly, "{BAD_REQUEST}"));
      return Http::FilterHeadersStatus::StopIteration;
    }
    if (checkIfTranscoderFailed(RcDetails::get().GrpcTranscodeFailed)) {
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  headers.removeContentLength();
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Grpc);
  headers.setEnvoyOriginalPath(headers.getPathValue());
  headers.addReferenceKey(Http::Headers::get().EnvoyOriginalMethod, headers.getMethodValue());
  headers.setPath("/" + method_->descriptor_->service()->full_name() + "/" +
                  method_->descriptor_->name());
  headers.setReferenceMethod(Http::Headers::get().MethodValues.Post);
  headers.setReferenceTE(Http::Headers::get().TEValues.Trailers);

  if (!per_route_config_->matchIncomingRequestInfo()) {
    decoder_callbacks_->clearRouteCache();
  }

  if (end_stream && method_->request_type_is_http_body_) {
    maybeSendHttpBodyRequestMessage();
  } else if (end_stream) {
    request_in_.finish();

    if (checkIfTranscoderFailed(RcDetails::get().GrpcTranscodeFailedEarly)) {
      return Http::FilterHeadersStatus::StopIteration;
    }

    Buffer::OwnedImpl data;
    readToBuffer(*transcoder_->RequestOutput(), data);

    if (data.length() > 0) {
      decoder_callbacks_->addDecodedData(data, true);
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus JsonTranscoderFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!error_);

  if (!transcoder_) {
    return Http::FilterDataStatus::Continue;
  }

  if (method_->request_type_is_http_body_) {
    request_data_.move(data);
    // TODO(euroelessar): Upper bound message size for streaming case.
    if (end_stream || method_->descriptor_->client_streaming()) {
      maybeSendHttpBodyRequestMessage();
    } else {
      // TODO(euroelessar): Avoid buffering if content length is already known.
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
  } else {
    request_in_.move(data);

    if (end_stream) {
      request_in_.finish();
    }

    readToBuffer(*transcoder_->RequestOutput(), data);
  }

  if (checkIfTranscoderFailed(RcDetails::get().GrpcTranscodeFailed)) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus JsonTranscoderFilter::decodeTrailers(Http::RequestTrailerMap&) {
  ASSERT(!error_);

  if (!transcoder_) {
    return Http::FilterTrailersStatus::Continue;
  }

  if (method_->request_type_is_http_body_) {
    maybeSendHttpBodyRequestMessage();
  } else {
    request_in_.finish();

    Buffer::OwnedImpl data;
    readToBuffer(*transcoder_->RequestOutput(), data);

    if (data.length()) {
      decoder_callbacks_->addDecodedData(data, true);
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus JsonTranscoderFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  if (!Grpc::Common::isGrpcResponseHeaders(headers, end_stream)) {
    error_ = true;
  }

  if (error_ || !transcoder_) {
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;

  if (end_stream) {
    if (method_->descriptor_->server_streaming()) {
      // When there is no body in a streaming response, a empty JSON array is
      // returned by default. Set the content type correctly.
      headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    }

    // In gRPC wire protocol, headers frame with end_stream is a trailers-only response.
    // The return value from encodeTrailers is ignored since it is always continue.
    doTrailers(headers);

    return Http::FilterHeadersStatus::Continue;
  }

  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  // In case of HttpBody in response - content type is unknown at this moment.
  // So "Continue" only for regular streaming use case and StopIteration for
  // all other cases (non streaming, streaming + httpBody)
  if (method_->descriptor_->server_streaming() && !method_->response_type_is_http_body_) {
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus JsonTranscoderFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (error_ || !transcoder_) {
    return Http::FilterDataStatus::Continue;
  }

  has_body_ = true;

  if (method_->response_type_is_http_body_) {
    bool frame_processed = buildResponseFromHttpBodyOutput(*response_headers_, data);
    if (!method_->descriptor_->server_streaming()) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    if (!http_body_response_headers_set_ && !frame_processed) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    return Http::FilterDataStatus::Continue;
  }

  response_in_.move(data);

  if (end_stream) {
    response_in_.finish();
  }

  readToBuffer(*transcoder_->ResponseOutput(), data);

  if (!method_->descriptor_->server_streaming() && !end_stream) {
    // Buffer until the response is complete.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
  // TODO(lizan): Check ResponseStatus

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus
JsonTranscoderFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  doTrailers(trailers);

  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::doTrailers(Http::ResponseHeaderOrTrailerMap& headers_or_trailers) {
  if (error_ || !transcoder_ || !per_route_config_ || per_route_config_->disabled()) {
    return;
  }

  response_in_.finish();

  const absl::optional<Grpc::Status::GrpcStatus> grpc_status =
      Grpc::Common::getGrpcStatus(headers_or_trailers, true);
  if (grpc_status && maybeConvertGrpcStatus(*grpc_status, headers_or_trailers)) {
    return;
  }

  if (!method_->response_type_is_http_body_) {
    Buffer::OwnedImpl data;
    readToBuffer(*transcoder_->ResponseOutput(), data);
    if (data.length()) {
      encoder_callbacks_->addEncodedData(data, true);
    }
  }

  // If there was no previous headers frame, this |trailers| map is our |response_headers_|,
  // so there is no need to copy headers from one to the other.
  const bool is_trailers_only_response = response_headers_ == &headers_or_trailers;
  const bool is_server_streaming = method_->descriptor_->server_streaming();

  if (is_server_streaming && !is_trailers_only_response) {
    // Continue if headers were sent already.
    return;
  }

  if (!grpc_status || grpc_status.value() == Grpc::Status::WellKnownGrpcStatus::InvalidCode) {
    response_headers_->setStatus(enumToInt(Http::Code::ServiceUnavailable));
  } else {
    response_headers_->setStatus(Grpc::Utility::grpcToHttpStatus(grpc_status.value()));
    if (!is_trailers_only_response) {
      response_headers_->setGrpcStatus(grpc_status.value());
    }
  }

  if (!is_trailers_only_response) {
    // Copy the grpc-message header if it exists.
    const Http::HeaderEntry* grpc_message_header = headers_or_trailers.GrpcMessage();
    if (grpc_message_header) {
      response_headers_->setGrpcMessage(grpc_message_header->value().getStringView());
    }
  }

  // remove Trailer headers if the client connection was http/1
  if (encoder_callbacks_->streamInfo().protocol() < Http::Protocol::Http2) {
    response_headers_->remove(trailerHeader());
  }

  if (!method_->descriptor_->server_streaming()) {
    // Set content-length for non-streaming responses.
    response_headers_->setContentLength(
        encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
  }
}

void JsonTranscoderFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool JsonTranscoderFilter::checkIfTranscoderFailed(const std::string& details) {
  const auto& request_status = transcoder_->RequestStatus();
  if (!request_status.ok()) {
    ENVOY_LOG(debug, "Transcoding request error {}", request_status.ToString());
    error_ = true;
    decoder_callbacks_->sendLocalReply(
        Http::Code::BadRequest,
        absl::string_view(request_status.error_message().data(),
                          request_status.error_message().size()),
        nullptr, absl::nullopt,
        absl::StrCat(details, "{", MessageUtil::CodeEnumToString(request_status.code()), "}"));

    return true;
  }
  return false;
}

// TODO(lizan): Incorporate watermarks to bound buffer sizes
bool JsonTranscoderFilter::readToBuffer(Protobuf::io::ZeroCopyInputStream& stream,
                                        Buffer::Instance& data) {
  const void* out;
  int size;
  while (stream.Next(&out, &size)) {
    data.add(out, size);

    if (size == 0) {
      return true;
    }
  }
  return false;
}

void JsonTranscoderFilter::maybeSendHttpBodyRequestMessage() {
  if (first_request_sent_ && request_data_.length() == 0) {
    return;
  }

  Buffer::OwnedImpl message_payload;
  message_payload.move(initial_request_data_);
  HttpBodyUtils::appendHttpBodyEnvelope(message_payload, method_->request_body_field_path,
                                        std::move(content_type_), request_data_.length());
  content_type_.clear();
  message_payload.move(request_data_);

  Envoy::Grpc::Encoder().prependFrameHeader(Envoy::Grpc::GRPC_FH_DEFAULT, message_payload);

  decoder_callbacks_->addDecodedData(message_payload, true);

  first_request_sent_ = true;
}

bool JsonTranscoderFilter::buildResponseFromHttpBodyOutput(
    Http::ResponseHeaderMap& response_headers, Buffer::Instance& data) {
  std::vector<Grpc::Frame> frames;
  decoder_.decode(data, frames);
  if (frames.empty()) {
    return false;
  }

  google::api::HttpBody http_body;
  for (auto& frame : frames) {
    if (frame.length_ > 0) {
      http_body.Clear();
      Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
      if (!HttpBodyUtils::parseMessageByFieldPath(&stream, method_->response_body_field_path,
                                                  &http_body)) {
        // TODO(euroelessar): Return error to client.
        encoder_callbacks_->resetStream();
        return true;
      }
      const auto& body = http_body.data();

      data.add(body);

      if (!method_->descriptor_->server_streaming()) {
        // Non streaming case: single message with content type / length
        response_headers.setContentType(http_body.content_type());
        response_headers.setContentLength(body.size());
        return true;
      } else if (!http_body_response_headers_set_) {
        // Streaming case: set content type only once from first HttpBody message
        response_headers.setContentType(http_body.content_type());
        http_body_response_headers_set_ = true;
      }
    }
  }

  return true;
}

bool JsonTranscoderFilter::maybeConvertGrpcStatus(Grpc::Status::GrpcStatus grpc_status,
                                                  Http::ResponseHeaderOrTrailerMap& trailers) {
  ASSERT(per_route_config_ && !per_route_config_->disabled());
  if (!per_route_config_->convertGrpcStatus()) {
    return false;
  }

  // Send a serialized status only if there was no body.
  if (has_body_) {
    return false;
  }

  if (grpc_status == Grpc::Status::WellKnownGrpcStatus::Ok ||
      grpc_status == Grpc::Status::WellKnownGrpcStatus::InvalidCode) {
    return false;
  }

  // TODO(mattklein123): The dynamic cast here is needed because ResponseHeaderOrTrailerMap is not
  // a header map. This can likely be cleaned up.
  auto status_details =
      Grpc::Common::getGrpcStatusDetailsBin(dynamic_cast<Http::HeaderMap&>(trailers));
  if (!status_details) {
    // If no rpc.Status object was sent in the grpc-status-details-bin header,
    // construct it from the grpc-status and grpc-message headers.
    status_details.emplace();
    status_details->set_code(grpc_status);

    auto grpc_message_header = trailers.GrpcMessage();
    if (grpc_message_header) {
      auto message = grpc_message_header->value().getStringView();
      status_details->set_message(message.data(), message.size());
    }
  }

  std::string json_status;
  auto translate_status =
      per_route_config_->translateProtoMessageToJson(*status_details, &json_status);
  if (!translate_status.ok()) {
    ENVOY_LOG(debug, "Transcoding status error {}", translate_status.ToString());
    return false;
  }

  response_headers_->setStatus(Grpc::Utility::grpcToHttpStatus(grpc_status));

  bool is_trailers_only_response = response_headers_ == &trailers;
  if (is_trailers_only_response) {
    // Drop the gRPC status headers, we already have them in the JSON body.
    response_headers_->removeGrpcStatus();
    response_headers_->removeGrpcMessage();
    response_headers_->remove(Http::Headers::get().GrpcStatusDetailsBin);
  }

  // remove Trailer headers if the client connection was http/1
  if (encoder_callbacks_->streamInfo().protocol() < Http::Protocol::Http2) {
    response_headers_->remove(trailerHeader());
  }

  response_headers_->setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  response_headers_->setContentLength(json_status.length());

  Buffer::OwnedImpl status_data(json_status);
  encoder_callbacks_->addEncodedData(status_data, false);
  return true;
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
