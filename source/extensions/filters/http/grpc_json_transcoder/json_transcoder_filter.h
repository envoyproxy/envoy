#pragma once

#include <memory>
#include <mutex>
#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/repeated_field.h>

#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_manager.h"
#include "source/common/grpc/typed_async_client.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/filters/http/grpc_json_transcoder/transcoder_input_stream_impl.h"
#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

#include "google/api/http.pb.h"
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
  std::vector<const ProtobufWkt::Field*> request_body_field_path;
  std::vector<const ProtobufWkt::Field*> response_body_field_path;
  bool request_type_is_http_body_ = false;
  bool response_type_is_http_body_ = false;
};
using MethodInfoSharedPtr = std::shared_ptr<MethodInfo>;

// Forward declarations
class DescriptorPoolBuilder;
class JsonTranscoderConfig;

/**
 * This class contains the logic for starting gRPC reflection Rpcs and handling
 * callbacks.
 */
class AsyncReflectionFetcher :
    public Envoy::Grpc::AsyncStreamCallbacks<grpc::reflection::v1alpha::ServerReflectionResponse> {
public:
  /**
   * Constructor. All arguments are used for needed for making non-blocking gRpc requests with the
   * Envoy client.
   */
  AsyncReflectionFetcher(const std::string& cluster_name, const google::protobuf::RepeatedPtrField<std::string>& services,
      TimeSource& time_source, Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager) :
      cluster_name_(cluster_name),
      time_source_(time_source),
      cluster_manager_(cluster_manager),
      init_manager_(init_manager),
      services_(std::begin(services), std::end(services)),
      async_clients_() {}

  /**
   * Ask this AsyncReflectionFetcher to call descriptor_pool_builder.
   */
  void requestFileDescriptors(DescriptorPoolBuilder* descriptor_pool_builder);

  /**
   * Kick off reflection Rpcs; invoked by initManager.
   */
  void startReflectionRpcs();

  /**
   * Callback for a successful Server Reflection response.
   */
  void onReceiveMessage(std::unique_ptr<grpc::reflection::v1alpha::ServerReflectionResponse>&& message);

  /**
   * See RawAsyncStreamCallbacks for documentation of these methods.
   */
  void onCreateInitialMetadata(Http::RequestHeaderMap&) {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) {};

  /**
   * These two are used for detecting unexpected errors.
   */
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Envoy::Grpc::Status::GrpcStatus status, const std::string& message) override;

private:
  /**
   * The name of the upstream Envoy cluster serving gRPC.
   */
  const std::string cluster_name_;

  /**
   * Time source from the factory context; used for making gRPC requests.
   */
  TimeSource& time_source_;

  /**
   * Reference to the upstream cluster managered, used for constructing a
   * Envoy::Grpc::AsyncClientImpl to make non-blocking Reflection Rpcs with.
   */
  Upstream::ClusterManager& cluster_manager_;

  /**
   * Reference to the factory context's init manager, used for deferring
   * completion of filter initialization until the reflection Rpcs complete.
   */
  Init::Manager& init_manager_;

  /**
   * A pointer to the DescriptorPoolBuilder that requested file descriptors from us.
   */
  DescriptorPoolBuilder* descriptor_pool_builder_;

  /**
   * The remaining services that we need protos for.
   */
  std::unordered_set<std::string> services_;

  /**
   * The async clients used to make each server reflection request. We are
   * using multiple clients to send multiple streaming Rpcs in parallel, as
   * recommended by qiwzhang.
   */
  std::vector<std::unique_ptr<Envoy::Grpc::AsyncClient<grpc::reflection::v1alpha::ServerReflectionRequest,
      grpc::reflection::v1alpha::ServerReflectionResponse> >> async_clients_;


  /**
   * The target for the initManager, used to notify the init manager that the
   * filter has finished initializing.
   */
  std::unique_ptr<Init::TargetImpl> init_target_;

  /**
   * The file descriptors we have received from reflection requests thus far.
   */
  Envoy::Protobuf::FileDescriptorSet file_descriptor_set_;

  /**
   * This mutex protects the data structures manipulated in the
   * onReceiveMessage callback, since the same object is used for handling all
   * the callbacks.
   */
  std::mutex receive_message_mutex_;
};

/**
 * This class contains the logic for building a protobuf descriptor pool from a
 * set of FileDescriptorProtos.
 */
class DescriptorPoolBuilder {
public:
  /**
   * Constructor for the non-reflection usage.
   */
  DescriptorPoolBuilder(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      Api::Api& api) :
    proto_config_(proto_config),
    api_(&api) {}

  /**
   * Constructor. The async_reflection_fetcher is injected rather than
   * constructed internally for easier unit testing.
   */
  DescriptorPoolBuilder(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      std::shared_ptr<AsyncReflectionFetcher> async_reflection_fetcher);

  /**
   * Ask this DescriptorPoolBuilder to call json_transcoder_config.setDescriptorPool at some point
   * the future. If this DescriptorPoolBuilder is using reflection, the requested call will
   * happen after the completion of reflection Rpcs. Otherwise, the requested call will happen
   * before this call returns.
   */
  void requestDescriptorPool(JsonTranscoderConfig* json_transcoder_config);

  /**
   * Convert the given FileDescriptorSet into a descriptor pool and inject it
   * into the json_transcoder_config passed to requestDescriptorPool.
   */
  void loadFileDescriptorProtos(const Envoy::Protobuf::FileDescriptorSet& file_descriptor_set);

private:
  /**
   * A copy of the original proto_config.
   */
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config_;

  /**
   * A pointer to our AsyncReflectionFetcher if we are using reflection. Set to nullptr if we are
   * not using reflection.
   */
  std::shared_ptr<AsyncReflectionFetcher> async_reflection_fetcher_;

  /**
   * A pointer to the JsonTranscoderConfig that requested a descriptor pool.
   * We inject our descriptor pool into this after we are done building it.
   * This is only necessary for reflection, although we use it everywhere for
   * consistency.
   */
  JsonTranscoderConfig* json_transcoder_config_;

  /**
   * API used for reading from the filesystem when loading protos from the filesystem.
   */
  Api::Api* api_;

  /**
   * The descriptor pool being built. This is mostly syntactic sugar to skip
   * passing this variable into addFileDescriptor. This variable is fully
   * consumed after being passed to json_transcoder_config_->setDescriptorPool.
   */
  Protobuf::DescriptorPool* descriptor_pool_;
};

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
      std::shared_ptr<DescriptorPoolBuilder> descriptor_pool_builder);

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
   * Transfer ownership of a Protobuf::DescriptorPool and its fallback database
   * into this class and enable the filter.
   */
  void loadDescriptorPoolAndDatabase(Protobuf::DescriptorPool* descriptor_pool,
      Protobuf::DescriptorDatabase* descriptor_database);

  /**
   * If true, when trailer indicates a gRPC error and there was no HTTP body,
   * make google.rpc.Status out of gRPC status headers and use it as JSON body.
   */
  bool convertGrpcStatus() const;

  bool disabled() const { return disabled_; }
  bool initialized() const { return initialized_; }

  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      RequestValidationOptions request_validation_options_{};

private:
  /**
   * Convert method descriptor to RequestInfo that needed for transcoding library
   */
  ProtobufUtil::Status methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                           google::grpc::transcoding::RequestInfo* info) const;

  ProtobufUtil::Status resolveField(const Protobuf::Descriptor* descriptor,
                                    const std::string& field_path_str,
                                    std::vector<const ProtobufWkt::Field*>* field_path,
                                    bool* is_http_body);
  ProtobufUtil::Status createMethodInfo(const Protobuf::MethodDescriptor* descriptor,
                                        const google::api::HttpRule& http_rule,
                                        MethodInfoSharedPtr& method_info);

  /**
   * A copy of the original proto_config.
   */
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder proto_config_;

  std::unique_ptr<Protobuf::DescriptorPool> descriptor_pool_;

  /**
   * This database is owned by the config because it is needed by
   * descriptor_pool_ but the latter does not want to own this object.
   */
  std::unique_ptr<Protobuf::DescriptorDatabase> descriptor_database_;
  google::grpc::transcoding::PathMatcherPtr<MethodInfoSharedPtr> path_matcher_;
  std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
  Protobuf::util::JsonPrintOptions print_options_;

  bool match_incoming_request_route_{false};
  bool ignore_unknown_query_parameters_{false};
  bool convert_grpc_status_{false};

  /**
   * True means that a filter using this config will pass through requests and
   * responses without modification.
   */
  bool disabled_;

  /**
   * This should be set if and only if the protobuf descriptor descriptor pool
   * has been passed into this class.
   * False means that a filter using this config will pass through requests and
   * responses without modification.
   */
  bool initialized_;

  /**
   * Used for building the descriptor pool for this config instance. We need to keep this alive in
   * case we are using gRPC reflection.
   */
  std::shared_ptr<DescriptorPoolBuilder> descriptor_pool_builder_;
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
  Http::FilterHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
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

  // Helpers for flow control.
  bool decoderBufferLimitReached(uint64_t buffer_length);
  bool encoderBufferLimitReached(uint64_t buffer_length);

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
