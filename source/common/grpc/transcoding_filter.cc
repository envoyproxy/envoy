#include <common/http/headers.h>
#include "common/grpc/transcoding_filter.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "envoy/common/exception.h"
#include "envoy/http/filter.h"
#include "google/api/annotations.pb.h"
#include "google/api/http.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/util/type_resolver.h"
#include "google/protobuf/util/type_resolver_util.h"
#include "src/json_request_translator.h"
#include "src/response_to_json_translator.h"

using google::grpc::transcoding::JsonRequestTranslator;
using google::grpc::transcoding::RequestInfo;
using google::grpc::transcoding::ResponseToJsonTranslator;
using google::grpc::transcoding::Transcoder;
using google::grpc::transcoding::TranscoderInputStream;
using google::protobuf::DescriptorPool;
using google::protobuf::FileDescriptor;
using google::protobuf::FileDescriptorSet;
using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::util::error::Code;
using google::protobuf::util::Status;

namespace Envoy {
namespace Grpc {

namespace {

const std::string kTypeUrlPrefix{"type.googleapis.com"};

// Transcoder implementation based on JsonRequestTranslator &
// ResponseToJsonTranslator
class TranscoderImpl : public Transcoder {
 public:
  // request_translator - a JsonRequestTranslator that does the request
  //                      translation
  // response_translator - a ResponseToJsonTranslator that does the response
  //                       translation
  TranscoderImpl(std::unique_ptr<JsonRequestTranslator> request_translator,
                 std::unique_ptr<ResponseToJsonTranslator> response_translator)
      : request_translator_(std::move(request_translator)),
        response_translator_(std::move(response_translator)),
        request_stream_(request_translator_->Output().CreateInputStream()),
        response_stream_(response_translator_->CreateInputStream()) {}

  // Transcoder implementation
  ::google::grpc::transcoding::TranscoderInputStream* RequestOutput() { return request_stream_.get(); }
  Status RequestStatus() { return request_translator_->Output().Status(); }

  ZeroCopyInputStream* ResponseOutput() { return response_stream_.get(); }
  Status ResponseStatus() { return response_translator_->Status(); }

 private:
  std::unique_ptr<JsonRequestTranslator> request_translator_;
  std::unique_ptr<ResponseToJsonTranslator> response_translator_;
  std::unique_ptr<::google::grpc::transcoding::TranscoderInputStream> request_stream_;
  std::unique_ptr<::google::grpc::transcoding::TranscoderInputStream> response_stream_;
};

} // namespace

TranscodingConfig::TranscodingConfig(const Json::Object& config) {
  std::string proto_descriptor_file = config.getString("proto_descriptor");
  FileDescriptorSet descriptor_set;
  if (!descriptor_set.ParseFromString(
      Filesystem::fileReadToEnd(proto_descriptor_file))) {
    throw EnvoyException("Unable to parse proto descriptor");
  }

  for (const auto& file : descriptor_set.file()) {
    if (descriptor_pool_.BuildFile(file) == nullptr) {
      throw EnvoyException("Unable to parse proto descriptor");
    }
  }

  google::grpc::transcoding::PathMatcherBuilder<
      const google::protobuf::MethodDescriptor*>
      pmb;

  for (const auto& service_name : config.getStringArray("services")) {
    auto service = descriptor_pool_.FindServiceByName(service_name);
    if (service == nullptr) {
      throw EnvoyException("Could not find '" + service_name +
          "' in the proto descriptor");
    }
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);

      auto http_rule = method->options().GetExtension(google::api::http);

      log().debug("/" + service->full_name() + "/" + method->name());
      log().debug(http_rule.DebugString());

      switch (http_rule.pattern_case()) {
        case ::google::api::HttpRule::kGet:
          pmb.Register("GET", http_rule.get(), http_rule.body(), method);
          break;
        case ::google::api::HttpRule::kPut:
          pmb.Register("PUT", http_rule.put(), http_rule.body(), method);
          break;
        case ::google::api::HttpRule::kPost:
          pmb.Register("POST", http_rule.post(), http_rule.body(), method);
          break;
        case ::google::api::HttpRule::kDelete:
          pmb.Register("DELETE", http_rule.delete_(), http_rule.body(), method);
          break;
        case ::google::api::HttpRule::kPatch:
          pmb.Register("PATCH", http_rule.patch(), http_rule.body(), method);
          break;
        case ::google::api::HttpRule::kCustom:
          pmb.Register(http_rule.custom().kind(), http_rule.custom().path(),
                       http_rule.body(), method);
          break;
        default:
          break;
      }
    }
  }

  path_matcher_ = pmb.Build();

  type_helper_.reset(new google::grpc::transcoding::TypeHelper(
      google::protobuf::util::NewTypeResolverForDescriptorPool(
          kTypeUrlPrefix, &descriptor_pool_)));

  log().debug("transcoding filter loaded");
}

Status TranscodingConfig::CreateTranscoder(
    const Http::HeaderMap& headers, ZeroCopyInputStream* request_input,
    ::google::grpc::transcoding::TranscoderInputStream* response_input,
    std::unique_ptr<Transcoder>& transcoder,
    const google::protobuf::MethodDescriptor*& method_descriptor) {
  std::string method = headers.Method()->value().c_str();
  std::string path = headers.Path()->value().c_str();
  std::string args;

  size_t pos = path.find('?');
  if (pos != std::string::npos) {
    args = path.substr(pos + 1);
    path = path.substr(0, pos);
  }

  RequestInfo request_info;
  std::vector<VariableBinding> variable_bidings;
  method_descriptor = path_matcher_->Lookup(
      method, path, args, &variable_bidings, &request_info.body_field_path);
  if (!method_descriptor) {
    return Status(Code::NOT_FOUND,
                  "Could not resolve " + path + " to a method");
  }

  auto status = MethodToRequestInfo(method_descriptor, &request_info);
  if (!status.ok()) {
    return status;
  }

  for (const auto& binding : variable_bidings) {
    google::grpc::transcoding::RequestWeaver::BindingInfo resolved_binding;
    auto status = type_helper_->ResolveFieldPath(*request_info.message_type,
                                                 binding.field_path,
                                                 &resolved_binding.field_path);
    if (!status.ok()) {
      return status;
    }

    resolved_binding.value = binding.value;

    log().debug("VALUE: " + resolved_binding.value);

    request_info.variable_bindings.emplace_back(std::move(resolved_binding));
  }

  std::unique_ptr<JsonRequestTranslator> request_translator{
      new JsonRequestTranslator(type_helper_->Resolver(), request_input,
                                request_info,
                                method_descriptor->client_streaming(), true)};

  auto response_type_url =
      kTypeUrlPrefix + "/" + method_descriptor->output_type()->full_name();
  std::unique_ptr<ResponseToJsonTranslator> response_translator{
      new ResponseToJsonTranslator(type_helper_->Resolver(), response_type_url,
                                   method_descriptor->server_streaming(),
                                   response_input)};

  transcoder.reset(new TranscoderImpl(std::move(request_translator),
                                      std::move(response_translator)));
  return Status::OK;
}

Status TranscodingConfig::MethodToRequestInfo(
    const google::protobuf::MethodDescriptor* method,
    google::grpc::transcoding::RequestInfo* info) {
  auto request_type_url =
      kTypeUrlPrefix + "/" + method->input_type()->full_name();
  info->message_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  if (info->message_type == nullptr) {
    log().debug("Cannot resolve input-type: {}",
                method->input_type()->full_name());
    return Status(Code::NOT_FOUND, "Could not resolve type: " +
        method->input_type()->full_name());
  }

  return Status::OK;
}


TranscodingFilter::TranscodingFilter(TranscodingConfig& config) : config_(config) {}

Http::FilterHeadersStatus TranscodingFilter::decodeHeaders(Http::HeaderMap& headers,
                                                           bool end_stream) {
  log().debug("Transcoding::Instance::decodeHeaders");

  auto status = config_.CreateTranscoder(headers, &request_in_, &response_in_,
                                         transcoder_, method_);
  if (status.ok()) {
    headers.removeContentLength();
    headers.insertContentType().value(Http::Headers::get().ContentTypeValues.Grpc);
    headers.insertPath().value("/" + method_->service()->full_name() + "/" +
        method_->name());

    headers.insertMethod().value(Http::Headers::get().MethodValues.Post);

    headers.insertTE().value(Http::Headers::get().TEValues.Trailers);

    if (end_stream) {
      log().debug("header only request");

      request_in_.Finish();

      const auto& request_status = transcoder_->RequestStatus();
      if (!request_status.ok()) {
        log().debug("Transcoding request error " + request_status.ToString());
        error_ = true;
        Http::Utility::sendLocalReply(
            *decoder_callbacks_, Http::Code::BadRequest,
            request_status.error_message().ToString());

        return Http::FilterHeadersStatus::StopIteration;
      }

      Buffer::OwnedImpl data;
      ReadToBuffer(transcoder_->RequestOutput(), data);

      if (data.length()) {
        decoder_callbacks_->addDecodedData(data);
      }
    }
  } else {
    log().debug("No transcoding: " + status.ToString());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus TranscodingFilter::decodeData(Buffer::Instance& data,
                                                     bool end_stream) {
  log().debug("Transcoding::Instance::decodeData");

  if (error_) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  if (transcoder_) {
    request_in_.Move(data);

    if (end_stream) {
      request_in_.Finish();
    }

    ReadToBuffer(transcoder_->RequestOutput(), data);

    const auto& request_status = transcoder_->RequestStatus();

    if (!request_status.ok()) {
      log().debug("Transcoding request error " + request_status.ToString());
      error_ = true;
      Http::Utility::sendLocalReply(*decoder_callbacks_, Http::Code::BadRequest,
                                    request_status.error_message().ToString());

      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus TranscodingFilter::decodeTrailers(Http::HeaderMap&) {
  log().debug("Transcoding::Instance::decodeTrailers");
  if (transcoder_) {
    request_in_.Finish();

    Buffer::OwnedImpl data;
    ReadToBuffer(transcoder_->RequestOutput(), data);

    if (data.length()) {
      decoder_callbacks_->addDecodedData(data);
    }
  }

  return Http::FilterTrailersStatus::Continue;
}

void TranscodingFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus TranscodingFilter::encodeHeaders(Http::HeaderMap& headers,
                                                           bool end_stream) {
  log().debug("Transcoding::Instance::encodeHeaders {}", end_stream);
  if (error_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (transcoder_) {
    response_headers_ = &headers;
    headers.insertContentType().value(Http::Headers::get().ContentTypeValues.Json);
    if (!method_->server_streaming() && !end_stream) {
      return Http::FilterHeadersStatus::StopIteration;
    }
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus TranscodingFilter::encodeData(Buffer::Instance& data,
                                                     bool end_stream) {
  log().debug("Transcoding::Instance::encodeData");
  if (error_) {
    return Http::FilterDataStatus::Continue;
  }

  if (transcoder_) {
    response_in_.Move(data);

    if (end_stream) {
      response_in_.Finish();
    }

    ReadToBuffer(transcoder_->ResponseOutput(), data);

    if (!method_->server_streaming()) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }
    // TODO(lizan): Check ResponseStatus
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus TranscodingFilter::encodeTrailers(Http::HeaderMap& trailers) {
  log().debug("Transcoding::Instance::encodeTrailers");
  if (transcoder_) {
    response_in_.Finish();

    Buffer::OwnedImpl data;
    ReadToBuffer(transcoder_->ResponseOutput(), data);

    if (data.length()) {
      encoder_callbacks_->addEncodedData(data);
    }

    if (!method_->server_streaming()) {
      const Http::HeaderEntry* grpc_status_header = trailers.GrpcStatus();
      if (grpc_status_header) {
        uint64_t grpc_status_code;
        if (!StringUtil::atoul(grpc_status_header->value().c_str(),
                               grpc_status_code)) {
          response_headers_->Status()->value(
              enumToInt(Http::Code::ServiceUnavailable));
        }
        response_headers_->insertGrpcStatus().value(*grpc_status_header);
      }

      const Http::HeaderEntry* grpc_message_header = trailers.GrpcMessage();
      if (grpc_message_header) {
        response_headers_->insertGrpcMessage().value(*grpc_message_header);
      }

      response_headers_->insertContentLength().value(
          encoder_callbacks_->encodingBuffer()
          ? encoder_callbacks_->encodingBuffer()->length()
          : 0);
    }
  }
  return Http::FilterTrailersStatus::Continue;
}

void TranscodingFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool TranscodingFilter::ReadToBuffer(google::protobuf::io::ZeroCopyInputStream* stream,
                                     Buffer::Instance& data) {
  const void* out;
  int size;
  while (stream->Next(&out, &size)) {
    data.add(out, size);

    if (size == 0) {
      return true;
    }
  }
  return false;
}


}  // namespace Grpc
}  // namespace Envoy