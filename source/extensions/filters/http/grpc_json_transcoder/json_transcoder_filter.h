#pragma once

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/grpc/codec.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/grpc_json_transcoder/transcoder_input_stream_impl.h"

#include "grpc_transcoding/path_matcher.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

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
  std::vector<std::string> field_path;
  // The value to be inserted.
  std::string value;
};

struct MethodInfo {
  const Protobuf::MethodDescriptor* descriptor_ = nullptr;
  std::vector<const Protobuf::Field*> request_body_field_path;
  std::vector<const Protobuf::Field*> response_body_field_path;
  bool request_type_is_http_body_ = false;
  bool response_type_is_http_body_ = false;
};
using MethodInfoSharedPtr = std::shared_ptr<MethodInfo>;

void createHttpBodyEnvelope(Buffer::Instance& output,
                            const std::vector<const Protobuf::Field*>& request_body_field_path,
                            std::string content_type, uint64_t content_length);

/**
 * Global configuration for the gRPC JSON transcoder filter. Factory for the Transcoder interface.
 */
class JsonTranscoderConfig : public Logger::Loggable<Logger::Id::config>,
                             public Router::RouteSpecificFilterConfig {

public:
  /**
   * constructor that loads protobuf descriptors from the file specified in the JSON config.
   * and construct a path matcher for HTTP path bindings.
   */
  JsonTranscoderConfig(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      Api::Api& api);

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
  createTranscoder(const Http::RequestHeaderMap& headers,
                   Protobuf::io::ZeroCopyInputStream& request_input,
                   google::grpc::transcoding::TranscoderInputStream& response_input,
                   std::unique_ptr<google::grpc::transcoding::Transcoder>& transcoder,
                   MethodInfoSharedPtr& method_info) const;

  /**
   * Converts an arbitrary protobuf message to JSON.
   */
  ProtobufUtil::Status translateProtoMessageToJson(const Protobuf::Message& message,
                                                   std::string* json_out) const;

  /**
   * If true, skip clearing the route cache after the incoming request has been modified.
   * This allows Envoy to select the upstream cluster based on the incoming request
   * rather than the outgoing.
   */
  bool matchIncomingRequestInfo() const;

  /**
   * If true, when trailer indicates a gRPC error and there was no HTTP body,
   * make google.rpc.Status out of gRPC status headers and use it as JSON body.
   */
  bool convertGrpcStatus() const;

  bool disabled() const { return disabled_; }

  bool strict_http_request_validation_{false};

private:
  /**
   * Convert method descriptor to RequestInfo that needed for transcoding library
   */
  ProtobufUtil::Status methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                           google::grpc::transcoding::RequestInfo* info) const;

  void addFileDescriptor(const Protobuf::FileDescriptorProto& file);
  void addBuiltinSymbolDescriptor(const std::string& symbol_name);
  ProtobufUtil::Status resolveField(const Protobuf::Descriptor* descriptor,
                                    const std::string& field_path_str,
                                    std::vector<const Protobuf::Field*>* field_path,
                                    bool* is_http_body);
  ProtobufUtil::Status createMethodInfo(const Protobuf::MethodDescriptor* descriptor,
                                        const google::api::HttpRule& http_rule,
                                        MethodInfoSharedPtr& method_info);

  Protobuf::DescriptorPool descriptor_pool_;
  google::grpc::transcoding::PathMatcherPtr<MethodInfoSharedPtr> path_matcher_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
  Protobuf::util::JsonPrintOptions print_options_;

  bool match_incoming_request_route_{false};
  bool ignore_unknown_query_parameters_{false};
  bool convert_grpc_status_{false};

  bool disabled_;
};

using JsonTranscoderConfigSharedPtr = std::shared_ptr<JsonTranscoderConfig>;

/**
 * The filter instance for gRPC JSON transcoder.
 */
class JsonTranscoderFilter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::http2> {
public:
  JsonTranscoderFilter(JsonTranscoderConfig& config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  // Http::StreamFilterBase
  void onDestroy() override {}

private:
  bool checkIfTranscoderFailed(const std::string& details);
  bool readToBuffer(Protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& data);
  void maybeSendHttpBodyRequestMessage();
  /**
   * Builds response from HttpBody protobuf.
   * Returns true if at least one gRPC frame has processed.
   */
  bool buildResponseFromHttpBodyOutput(Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& data);
  bool maybeConvertGrpcStatus(Grpc::Status::GrpcStatus grpc_status,
                              Http::ResponseHeaderOrTrailerMap& trailers);
  bool hasHttpBodyAsOutputType();
  void doTrailers(Http::ResponseHeaderOrTrailerMap& headers_or_trailers);
  void initPerRouteConfig();

  JsonTranscoderConfig& config_;
  const JsonTranscoderConfig* per_route_config_{};
  std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
  TranscoderInputStreamImpl request_in_;
  TranscoderInputStreamImpl response_in_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  MethodInfoSharedPtr method_;
  Http::ResponseHeaderMap* response_headers_{};
  Grpc::Decoder decoder_;

  // Data of the initial request message, initialized from query arguments, path, etc.
  Buffer::OwnedImpl initial_request_data_;
  Buffer::OwnedImpl request_data_;
  bool first_request_sent_{false};
  std::string content_type_;

  bool error_{false};
  bool has_body_{false};
  bool http_body_response_headers_set_{false};
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
