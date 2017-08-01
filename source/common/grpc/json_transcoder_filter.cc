#include "common/grpc/json_transcoder_filter.h"

#include "envoy/common/exception.h"
#include "envoy/http/filter.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/path_matcher_utility.h"
#include "grpc_transcoding/response_to_json_translator.h"

using Envoy::Protobuf::DescriptorPool;
using Envoy::Protobuf::FileDescriptor;
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::io::ZeroCopyInputStream;
using Envoy::ProtobufUtil::Status;
using Envoy::ProtobufUtil::error::Code;
using google::grpc::transcoding::JsonRequestTranslator;
using google::grpc::transcoding::PathMatcherBuilder;
using google::grpc::transcoding::PathMatcherUtility;
using google::grpc::transcoding::RequestInfo;
using google::grpc::transcoding::ResponseToJsonTranslator;
using google::grpc::transcoding::Transcoder;
using google::grpc::transcoding::TranscoderInputStream;

namespace Envoy {
namespace Grpc {

namespace {

const std::string TYPE_URL_PREFIX{"type.googleapis.com"};

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
  TranscoderImpl(std::unique_ptr<JsonRequestTranslator> request_translator,
                 std::unique_ptr<ResponseToJsonTranslator> response_translator)
      : request_translator_(std::move(request_translator)),
        response_translator_(std::move(response_translator)),
        request_stream_(request_translator_->Output().CreateInputStream()),
        response_stream_(response_translator_->CreateInputStream()) {}

  // Transcoder
  ::google::grpc::transcoding::TranscoderInputStream* RequestOutput() {
    return request_stream_.get();
  }
  Status RequestStatus() { return request_translator_->Output().Status(); }

  ZeroCopyInputStream* ResponseOutput() { return response_stream_.get(); }
  Status ResponseStatus() { return response_translator_->Status(); }

private:
  std::unique_ptr<JsonRequestTranslator> request_translator_;
  std::unique_ptr<ResponseToJsonTranslator> response_translator_;
  std::unique_ptr<TranscoderInputStream> request_stream_;
  std::unique_ptr<TranscoderInputStream> response_stream_;
};

} // namespace

JsonTranscoderConfig::JsonTranscoderConfig(const Json::Object& config) {
  const std::string proto_descriptor_file = config.getString("proto_descriptor");
  FileDescriptorSet descriptor_set;
  if (!descriptor_set.ParseFromString(Filesystem::fileReadToEnd(proto_descriptor_file))) {
    throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
  }

  for (const auto& file : descriptor_set.file()) {
    if (descriptor_pool_.BuildFile(file) == nullptr) {
      throw EnvoyException("transcoding_filter: Unable to build proto descriptor pool");
    }
  }

  PathMatcherBuilder<const Protobuf::MethodDescriptor*> pmb;

  for (const auto& service_name : config.getStringArray("services")) {
    auto service = descriptor_pool_.FindServiceByName(service_name);
    if (service == nullptr) {
      throw EnvoyException("transcoding_filter: Could not find '" + service_name +
                           "' in the proto descriptor");
    }
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      if (!PathMatcherUtility::RegisterByHttpRule(
              pmb, method->options().GetExtension(google::api::http), method)) {
        throw EnvoyException("transcoding_filter: Cannot register '" + method->full_name() +
                             "' to path matcher");
      }
    }
  }

  path_matcher_ = pmb.Build();

  type_helper_.reset(new google::grpc::transcoding::TypeHelper(
      Protobuf::util::NewTypeResolverForDescriptorPool(TYPE_URL_PREFIX, &descriptor_pool_)));

  const auto print_config = config.getObject("print_options", true);
  print_options_.add_whitespace = print_config->getBoolean("add_whitespace", false);
  print_options_.always_print_primitive_fields =
      print_config->getBoolean("always_print_primitive_fields", false);
  print_options_.always_print_enums_as_ints =
      print_config->getBoolean("always_print_enums_as_ints", false);
  print_options_.preserve_proto_field_names =
      print_config->getBoolean("preserve_proto_field_names", false);
}

Status JsonTranscoderConfig::createTranscoder(
    const Http::HeaderMap& headers, ZeroCopyInputStream& request_input,
    google::grpc::transcoding::TranscoderInputStream& response_input,
    std::unique_ptr<Transcoder>& transcoder, const Protobuf::MethodDescriptor*& method_descriptor) {
  const ProtobufTypes::String method = headers.Method()->value().c_str();
  ProtobufTypes::String path = headers.Path()->value().c_str();
  ProtobufTypes::String args;

  const size_t pos = path.find('?');
  if (pos != std::string::npos) {
    args = path.substr(pos + 1);
    path = path.substr(0, pos);
  }

  RequestInfo request_info;
  std::vector<VariableBinding> variable_bindings;
  method_descriptor =
      path_matcher_->Lookup(method, path, args, &variable_bindings, &request_info.body_field_path);
  if (!method_descriptor) {
    return Status(Code::NOT_FOUND, "Could not resolve " + path + " to a method");
  }

  Status status = methodToRequestInfo(method_descriptor, &request_info);
  if (!status.ok()) {
    return status;
  }

  for (const auto& binding : variable_bindings) {
    google::grpc::transcoding::RequestWeaver::BindingInfo resolved_binding;
    status = type_helper_->ResolveFieldPath(*request_info.message_type, binding.field_path,
                                            &resolved_binding.field_path);
    if (!status.ok()) {
      return status;
    }

    resolved_binding.value = binding.value;

    request_info.variable_bindings.emplace_back(std::move(resolved_binding));
  }

  std::unique_ptr<JsonRequestTranslator> request_translator{
      new JsonRequestTranslator(type_helper_->Resolver(), &request_input, request_info,
                                method_descriptor->client_streaming(), true)};

  const auto response_type_url =
      TYPE_URL_PREFIX + "/" + method_descriptor->output_type()->full_name();
  std::unique_ptr<ResponseToJsonTranslator> response_translator{new ResponseToJsonTranslator(
      type_helper_->Resolver(), response_type_url, method_descriptor->server_streaming(),
      &response_input, print_options_)};

  transcoder.reset(
      new TranscoderImpl(std::move(request_translator), std::move(response_translator)));
  return Status();
}

Status JsonTranscoderConfig::methodToRequestInfo(const Protobuf::MethodDescriptor* method,
                                                 google::grpc::transcoding::RequestInfo* info) {
  auto request_type_url = TYPE_URL_PREFIX + "/" + method->input_type()->full_name();
  info->message_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  if (info->message_type == nullptr) {
    ENVOY_LOG(debug, "Cannot resolve input-type: {}",
              ProtobufTypes::FromString(method->input_type()->full_name()));
    return Status(Code::NOT_FOUND, "Could not resolve type: " + method->input_type()->full_name());
  }

  return Status();
}

JsonTranscoderFilter::JsonTranscoderFilter(JsonTranscoderConfig& config) : config_(config) {}

Http::FilterHeadersStatus JsonTranscoderFilter::decodeHeaders(Http::HeaderMap& headers,
                                                              bool end_stream) {
  const auto status =
      config_.createTranscoder(headers, request_in_, response_in_, transcoder_, method_);

  if (!status.ok()) {
    // If transcoder couldn't be created, it might be a normal gRPC request, so the filter will
    // just pass-through the request to upstream.
    return Http::FilterHeadersStatus::Continue;
  }

  headers.removeContentLength();
  headers.insertContentType().value().setStatic(Http::Headers::get().ContentTypeValues.Grpc);
  headers.insertPath().value("/" + method_->service()->full_name() + "/" + method_->name());
  headers.insertMethod().value().setStatic(Http::Headers::get().MethodValues.Post);
  headers.insertTE().value().setStatic(Http::Headers::get().TEValues.Trailers);

  if (end_stream) {
    request_in_.finish();

    const auto& request_status = transcoder_->RequestStatus();
    if (!request_status.ok()) {
      ENVOY_LOG(debug, "Transcoding request error " +
                           ProtobufTypes::FromString(request_status.ToString()));
      error_ = true;
      Http::Utility::sendLocalReply(*decoder_callbacks_, stream_reset_, Http::Code::BadRequest,
                                    request_status.error_message());

      return Http::FilterHeadersStatus::StopIteration;
    }

    Buffer::OwnedImpl data;
    readToBuffer(*transcoder_->RequestOutput(), data);

    if (data.length() > 0) {
      decoder_callbacks_->addDecodedData(data);
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus JsonTranscoderFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ASSERT(!error_);

  if (!transcoder_) {
    return Http::FilterDataStatus::Continue;
  }

  request_in_.move(data);

  if (end_stream) {
    request_in_.finish();
  }

  readToBuffer(*transcoder_->RequestOutput(), data);

  const auto& request_status = transcoder_->RequestStatus();

  if (!request_status.ok()) {
    ENVOY_LOG(debug,
              "Transcoding request error " + ProtobufTypes::FromString(request_status.ToString()));
    error_ = true;
    Http::Utility::sendLocalReply(*decoder_callbacks_, stream_reset_, Http::Code::BadRequest,
                                  request_status.error_message());

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus JsonTranscoderFilter::decodeTrailers(Http::HeaderMap&) {
  ASSERT(!error_);

  if (!transcoder_) {
    return Http::FilterTrailersStatus::Continue;
  }

  request_in_.finish();

  Buffer::OwnedImpl data;
  readToBuffer(*transcoder_->RequestOutput(), data);

  if (data.length()) {
    decoder_callbacks_->addDecodedData(data);
  }
  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus JsonTranscoderFilter::encodeHeaders(Http::HeaderMap& headers,
                                                              bool end_stream) {
  if (error_ || !transcoder_) {
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  headers.insertContentType().value().setStatic(Http::Headers::get().ContentTypeValues.Json);
  if (!method_->server_streaming() && !end_stream) {
    return Http::FilterHeadersStatus::StopIteration;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus JsonTranscoderFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (error_ || !transcoder_) {
    return Http::FilterDataStatus::Continue;
  }

  response_in_.move(data);

  if (end_stream) {
    response_in_.finish();
  }

  readToBuffer(*transcoder_->ResponseOutput(), data);

  if (!method_->server_streaming()) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
  // TODO(lizan): Check ResponseStatus

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus JsonTranscoderFilter::encodeTrailers(Http::HeaderMap& trailers) {
  if (error_ || !transcoder_) {
    return Http::FilterTrailersStatus::Continue;
  }

  response_in_.finish();

  Buffer::OwnedImpl data;
  readToBuffer(*transcoder_->ResponseOutput(), data);

  if (data.length()) {
    encoder_callbacks_->addEncodedData(data);
  }

  if (method_->server_streaming()) {
    // For streaming case, the headers are already sent, so just continue here.
    return Http::FilterTrailersStatus::Continue;
  }

  const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
  if (grpc_status_header) {
    uint64_t grpc_status_code;
    if (!StringUtil::atoul(grpc_status_header->value().c_str(), grpc_status_code)) {
      response_headers_->Status()->value(enumToInt(Http::Code::ServiceUnavailable));
    }
    response_headers_->insertGrpcStatus().value(*grpc_status_header);
  }

  const Http::HeaderEntry* grpc_message_header = trailers.GrpcMessage();
  if (grpc_message_header) {
    response_headers_->insertGrpcMessage().value(*grpc_message_header);
  }

  response_headers_->insertContentLength().value(
      encoder_callbacks_->encodingBuffer() ? encoder_callbacks_->encodingBuffer()->length() : 0);
  return Http::FilterTrailersStatus::Continue;
}

void JsonTranscoderFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
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

} // namespace Grpc
} // namespace Envoy
