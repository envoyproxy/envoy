#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/config/filter/http/transcoder/v2/transcoder.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"

#include "common/common/logger.h"
#include "common/grpc/transcoder_input_stream_impl.h"
#include "common/protobuf/protobuf.h"

#include "grpc_transcoding/path_matcher.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Grpc {

/**
 * VariableBinding specifies a value for a single field in the request message.
 * When transcoding HTTP/REST/JSON to gRPC/proto the request message is
 * constructed using the HTTP body and the variable bindings (specified through
 * request url).
 * See https://github.com/googleapis/googleapis/blob/master/google/api/http.proto
 * for details of variable binding.
 */
struct VariableBinding {
  // The location of the field in the protobuf message, where the value
  // needs to be inserted, e.g. "shelf.theme" would mean the "theme" field
  // of the nested "shelf" message of the request protobuf message.
  std::vector<ProtobufTypes::String> field_path;
  // The value to be inserted.
  ProtobufTypes::String value;
};

/**
 * Global configuration for the gRPC JSON transcoder filter. Factory for the Transcoder interface.
 */
class JsonTranscoderConfig : public Logger::Loggable<Logger::Id::config> {
public:
  /**
   * constructor that loads protobuf descriptors from the file specified in the JSON config.
   * and construct a path matcher for HTTP path bindings.
   */
  JsonTranscoderConfig(
      const envoy::config::filter::http::transcoder::v2::GrpcJsonTranscoder& proto_config);

  /**
   * Create an instance of Transcoder interface based on incoming request
   * @param headers headers received from decoder
   * @param request_input a ZeroCopyInputStream reading from downstream request body
   * @param response_input a TranscoderInputStream reading from upstream response body
   * @param transcoder output parameter for the instance of Transcoder interface
   * @param method_descriptor output parameter for the method looked up from config
   * @return status whether the Transcoder instance are successfully created or not
   */
  ProtobufUtil::Status
  createTranscoder(const Http::HeaderMap& headers, Protobuf::io::ZeroCopyInputStream& request_input,
                   google::grpc::transcoding::TranscoderInputStream& response_input,
                   std::unique_ptr<google::grpc::transcoding::Transcoder>& transcoder,
                   const Protobuf::MethodDescriptor*& method_descriptor);

private:
  /**
   * Convert method descriptor to RequestInfo that needed for transcoding library
   */
  ProtobufUtil::Status methodToRequestInfo(const Protobuf::MethodDescriptor* method,
                                           google::grpc::transcoding::RequestInfo* info);

private:
  Protobuf::DescriptorPool descriptor_pool_;
  google::grpc::transcoding::PathMatcherPtr<const Protobuf::MethodDescriptor*> path_matcher_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
  Protobuf::util::JsonPrintOptions print_options_;
};

typedef std::shared_ptr<JsonTranscoderConfig> JsonTranscoderConfigSharedPtr;

/**
 * The filter instance for gRPC JSON transcoder.
 */
class JsonTranscoderFilter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::http2> {
public:
  JsonTranscoderFilter(JsonTranscoderConfig& config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // Http::StreamFilterBase
  void onDestroy() override { stream_reset_ = true; }

private:
  bool readToBuffer(Protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& data);

  JsonTranscoderConfig& config_;
  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_;
  TranscoderInputStreamImpl response_in_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  const Protobuf::MethodDescriptor* method_{nullptr};
  Http::HeaderMap* response_headers_{nullptr};

  bool error_{false};
  bool stream_reset_{false};
};

} // namespace Grpc
} // namespace Envoy
