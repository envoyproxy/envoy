#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_json_transcoder/stats.h"

#include "google/api/http.pb.h"
#include "grpc_transcoding/path_matcher.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/response_to_json_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/type_helper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

struct MethodInfo {
  const Protobuf::MethodDescriptor* descriptor_ = nullptr;
  std::vector<const Protobuf::Field*> request_body_field_path;
  std::vector<const Protobuf::Field*> response_body_field_path;
  bool request_type_is_http_body_ = false;
  bool response_type_is_http_body_ = false;
};
using MethodInfoSharedPtr = std::shared_ptr<MethodInfo>;

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

  // grpc by default doesn't like a frame larger than 4MB. Splitting streamed data
  // into 1MB pieces should keep that threshold from being exceeded when data comes
  // in as a large buffer.
  static constexpr size_t MaxStreamedPieceSize = 1024 * 1024;

  /**
   * Create an instance of Transcoder interface based on incoming request.
   * @param headers headers received from decoder.
   * @param request_input a ZeroCopyInputStream reading from downstream request body.
   * @param response_input a TranscoderInputStream reading from upstream response body.
   * @param transcoder output parameter for the instance of Transcoder interface.
   * @param method_descriptor output parameter for the method looked up from config.
   * @return status whether the Transcoder instance are successfully created or not. If the method
   *         is not found, status with Code::NOT_FOUND is returned. If the method is found, but
   * fields cannot be resolved, status with Code::INVALID_ARGUMENT is returned.
   */
  absl::Status
  createTranscoder(const Http::RequestHeaderMap& headers,
                   Protobuf::io::ZeroCopyInputStream& request_input,
                   google::grpc::transcoding::TranscoderInputStream& response_input,
                   std::unique_ptr<google::grpc::transcoding::Transcoder>& transcoder,
                   MethodInfoSharedPtr& method_info,
                   envoy::extensions::filters::http::grpc_json_transcoder::v3::UnknownQueryParams&
                       unknown_params) const;

  /**
   * Converts an arbitrary protobuf message to JSON.
   */
  absl::Status translateProtoMessageToJson(const Protobuf::Message& message,
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

  bool isStreamSSEStyleDelimited() const {
    return response_translate_options_.stream_sse_style_delimited;
  }

  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      RequestValidationOptions request_validation_options_{};

  absl::optional<uint32_t> max_request_body_size_;
  absl::optional<uint32_t> max_response_body_size_;

  void addBuiltinSymbolDescriptor(const std::string& symbol_name);

private:
  /**
   * Convert method descriptor to RequestInfo that needed for transcoding library
   */
  absl::Status methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                   google::grpc::transcoding::RequestInfo* info) const;

  void addFileDescriptor(const Protobuf::FileDescriptorProto& file);
  absl::Status resolveField(const Protobuf::Descriptor* descriptor,
                            const std::string& field_path_str,
                            std::vector<const Protobuf::Field*>* field_path, bool* is_http_body);
  absl::Status createMethodInfo(const Protobuf::MethodDescriptor* descriptor,
                                const google::api::HttpRule& http_rule,
                                MethodInfoSharedPtr& method_info);

  Protobuf::DescriptorPool descriptor_pool_;
  google::grpc::transcoding::PathMatcherPtr<MethodInfoSharedPtr> path_matcher_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
  google::grpc::transcoding::JsonResponseTranslateOptions response_translate_options_;

  bool match_incoming_request_route_{false};
  bool ignore_unknown_query_parameters_{false};
  bool capture_unknown_query_parameters_{false};
  bool convert_grpc_status_{false};
  bool case_insensitive_enum_parsing_{false};

  bool disabled_;
};

using JsonTranscoderConfigSharedPtr = std::shared_ptr<JsonTranscoderConfig>;
using JsonTranscoderConfigConstSharedPtr = std::shared_ptr<const JsonTranscoderConfig>;

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
