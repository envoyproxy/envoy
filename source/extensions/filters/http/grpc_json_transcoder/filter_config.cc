#include "source/extensions/filters/http/grpc_json_transcoder/filter_config.h"

#include <memory>
#include <unordered_set>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"
#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "google/api/httpbody.pb.h"
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/path_matcher_utility.h"
#include "grpc_transcoding/response_to_json_translator.h"

using absl::Status;
using absl::StatusCode;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::io::ZeroCopyInputStream;
using google::api::HttpRule;
using google::grpc::transcoding::JsonRequestTranslator;
using JsonRequestTranslatorPtr = std::unique_ptr<JsonRequestTranslator>;
using google::grpc::transcoding::MessageStream;
using google::grpc::transcoding::PathMatcherBuilder;
using google::grpc::transcoding::PathMatcherUtility;
using google::grpc::transcoding::RequestMessageTranslator;
using RequestMessageTranslatorPtr = std::unique_ptr<RequestMessageTranslator>;
using google::grpc::transcoding::ResponseToJsonTranslator;
using ResponseToJsonTranslatorPtr = std::unique_ptr<ResponseToJsonTranslator>;
using google::grpc::transcoding::Transcoder;
using TranscoderPtr = std::unique_ptr<Transcoder>;
using google::grpc::transcoding::TranscoderInputStream;
using TranscoderInputStreamPtr = std::unique_ptr<TranscoderInputStream>;
using envoy::extensions::filters::http::grpc_json_transcoder::v3::UnknownQueryParams;
using google::grpc::transcoding::VariableBinding;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

namespace {

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
  absl::Status RequestStatus() override { return request_message_stream_.Status(); }

  ZeroCopyInputStream* ResponseOutput() override { return response_stream_.get(); }
  absl::Status ResponseStatus() override { return response_translator_->Status(); }

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
      DescriptorSetCase::kProtoDescriptor: {
    auto file_or_error = api.fileSystem().fileReadToEnd(proto_config.proto_descriptor());
    THROW_IF_NOT_OK_REF(file_or_error.status());
    if (!descriptor_set.ParseFromString(file_or_error.value())) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    break;
  }
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kProtoDescriptorBin:
    if (!descriptor_set.ParseFromString(proto_config.proto_descriptor_bin())) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::DESCRIPTOR_SET_NOT_SET:
    throw EnvoyException("transcoding_filter: descriptor not set");
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
        auto post = absl::StrCat("/", service->full_name(), "/", method->name());
        http_rule.set_post(post);
        http_rule.set_body("*");
      }

      MethodInfoSharedPtr method_info;
      Status status = createMethodInfo(method, http_rule, method_info);
      if (!status.ok()) {
        throw EnvoyException(absl::StrCat("transcoding_filter: Cannot register '",
                                          method->full_name(), "': ", status.message()));
      }

      if (!PathMatcherUtility::RegisterByHttpRule(pmb, http_rule, ignored_query_parameters,
                                                  method_info)) {
        throw EnvoyException(absl::StrCat("transcoding_filter: Cannot register '",
                                          method->full_name(), "' to path matcher"));
      }
    }
  }

  switch (proto_config.url_unescape_spec()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
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
  }
  pmb.SetQueryParamUnescapePlus(proto_config.query_param_unescape_plus());
  pmb.SetMatchUnregisteredCustomVerb(proto_config.match_unregistered_custom_verb());

  path_matcher_ = pmb.Build();

  const auto& print_config = proto_config.print_options();
  response_translate_options_.json_print_options.add_whitespace = print_config.add_whitespace();
  response_translate_options_.json_print_options.always_print_fields_with_no_presence =
      print_config.always_print_primitive_fields();
  response_translate_options_.json_print_options.always_print_enums_as_ints =
      print_config.always_print_enums_as_ints();
  response_translate_options_.json_print_options.preserve_proto_field_names =
      print_config.preserve_proto_field_names();
  response_translate_options_.stream_newline_delimited = print_config.stream_newline_delimited();
  response_translate_options_.stream_sse_style_delimited =
      print_config.stream_sse_style_delimited();

  match_incoming_request_route_ = proto_config.match_incoming_request_route();
  ignore_unknown_query_parameters_ = proto_config.ignore_unknown_query_parameters();
  capture_unknown_query_parameters_ = proto_config.capture_unknown_query_parameters();
  request_validation_options_ = proto_config.request_validation_options();
  case_insensitive_enum_parsing_ = proto_config.case_insensitive_enum_parsing();
  if (proto_config.has_max_request_body_size()) {
    max_request_body_size_ = proto_config.max_request_body_size().value();
  }
  if (proto_config.has_max_response_body_size()) {
    max_response_body_size_ = proto_config.max_response_body_size().value();
  }
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
    return {StatusCode::kNotFound,
            absl::StrCat("Could not resolve type: ", descriptor->full_name())};
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
  return {};
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
    return {StatusCode::kUnimplemented,
            absl::StrCat("Setting \"response_body\" is not supported yet for non-HttpBody fields: ",
                         descriptor->full_name())};
  }

  return {};
}

bool JsonTranscoderConfig::matchIncomingRequestInfo() const {
  return match_incoming_request_route_;
}

bool JsonTranscoderConfig::convertGrpcStatus() const { return convert_grpc_status_; }

absl::Status JsonTranscoderConfig::createTranscoder(
    const Http::RequestHeaderMap& headers, ZeroCopyInputStream& request_input,
    google::grpc::transcoding::TranscoderInputStream& response_input,
    std::unique_ptr<Transcoder>& transcoder, MethodInfoSharedPtr& method_info,
    UnknownQueryParams& unknown_params) const {

  ASSERT(!disabled_);
  const std::string method(headers.getMethodValue());
  std::string path(headers.getPathValue());
  std::string args;

  const size_t pos = path.find('?');
  if (pos != std::string::npos) {
    args = path.substr(pos + 1);
    path = path.substr(0, pos);
  }

  google::grpc::transcoding::RequestInfo request_info;
  request_info.reject_binding_body_field_collisions =
      request_validation_options_.reject_binding_body_field_collisions();
  request_info.case_insensitive_enum_parsing = case_insensitive_enum_parsing_;
  std::vector<VariableBinding> variable_bindings;
  method_info =
      path_matcher_->Lookup(method, path, args, &variable_bindings, &request_info.body_field_path);
  if (!method_info) {
    return {StatusCode::kNotFound, "Could not resolve " + path + " to a method."};
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
      if (capture_unknown_query_parameters_) {
        auto binding_key = absl::StrJoin(binding.field_path, ".");
        (*unknown_params.mutable_key())[binding_key].add_values(binding.value);
        continue;
      } else if (ignore_unknown_query_parameters_) {
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
    request_translator->Input().StartObject("")->EndObject();
  } else {
    json_request_translator = std::make_unique<JsonRequestTranslator>(
        type_helper_->Resolver(), &request_input, std::move(request_info),
        method_info->descriptor_->client_streaming(), true);
  }

  const auto response_type_url =
      Grpc::Common::typeUrl(method_info->descriptor_->output_type()->full_name());
  ResponseToJsonTranslatorPtr response_translator{new ResponseToJsonTranslator(
      type_helper_->Resolver(), response_type_url, method_info->descriptor_->server_streaming(),
      &response_input, response_translate_options_)};

  transcoder = std::make_unique<TranscoderImpl>(std::move(request_translator),
                                                std::move(json_request_translator),
                                                std::move(response_translator));
  return {};
}

absl::Status
JsonTranscoderConfig::methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                          google::grpc::transcoding::RequestInfo* info) const {
  absl::string_view request_type_full_name = method_info->descriptor_->input_type()->full_name();
  auto request_type_url = Grpc::Common::typeUrl(request_type_full_name);
  info->message_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  if (info->message_type == nullptr) {
    ENVOY_LOG(debug, "Cannot resolve input-type: {}", request_type_full_name);
    return {StatusCode::kNotFound,
            absl::StrCat("Could not resolve type: ", request_type_full_name)};
  }

  return {};
}

absl::Status JsonTranscoderConfig::translateProtoMessageToJson(const Protobuf::Message& message,
                                                               std::string* json_out) const {
  return ProtobufUtil::BinaryToJsonString(
      type_helper_->Resolver(), Grpc::Common::typeUrl(message.GetDescriptor()->full_name()),
      message.SerializeAsString(), json_out, response_translate_options_.json_print_options);
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
