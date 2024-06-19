#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/ext_proc/client.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"
#include "source/extensions/filters/http/ext_proc/processor_state.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

#define ALL_EXT_PROC_FILTER_STATS(COUNTER)                                                         \
  COUNTER(streams_started)                                                                         \
  COUNTER(stream_msgs_sent)                                                                        \
  COUNTER(stream_msgs_received)                                                                    \
  COUNTER(spurious_msgs_received)                                                                  \
  COUNTER(streams_closed)                                                                          \
  COUNTER(streams_failed)                                                                          \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(message_timeouts)                                                                        \
  COUNTER(rejected_header_mutations)                                                               \
  COUNTER(override_message_timeout_received)                                                       \
  COUNTER(override_message_timeout_ignored)                                                        \
  COUNTER(clear_route_cache_ignored)                                                               \
  COUNTER(clear_route_cache_disabled)                                                              \
  COUNTER(clear_route_cache_upstream_ignored)                                                      \
  COUNTER(send_immediate_resp_upstream_ignored)

struct ExtProcFilterStats {
  ALL_EXT_PROC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class ExtProcLoggingInfo : public Envoy::StreamInfo::FilterState::Object {
public:
  explicit ExtProcLoggingInfo(const Envoy::ProtobufWkt::Struct& filter_metadata)
      : filter_metadata_(filter_metadata) {}

  // gRPC call stats for headers and trailers.
  struct GrpcCall {
    GrpcCall(const std::chrono::microseconds latency, const Grpc::Status::GrpcStatus call_status)
        : latency_(latency), call_status_(call_status) {}
    const std::chrono::microseconds latency_;
    const Grpc::Status::GrpcStatus call_status_;
  };

  // gRPC call stats for body.
  struct GrpcCallBody {
    GrpcCallBody(const uint32_t call_count, const Grpc::Status::GrpcStatus call_status,
                 const std::chrono::microseconds total_latency,
                 const std::chrono::microseconds max_latency,
                 const std::chrono::microseconds min_latency)
        : call_count_(call_count), last_call_status_(call_status), total_latency_(total_latency),
          max_latency_(max_latency), min_latency_(min_latency) {}
    uint32_t call_count_;
    Grpc::Status::GrpcStatus last_call_status_;
    std::chrono::microseconds total_latency_;
    std::chrono::microseconds max_latency_;
    std::chrono::microseconds min_latency_;
  };

  struct GrpcCallStats {
    std::unique_ptr<GrpcCall> header_stats_;
    std::unique_ptr<GrpcCall> trailer_stats_;
    std::unique_ptr<GrpcCallBody> body_stats_;
  };

  using GrpcCalls = struct GrpcCallStats;

  void recordGrpcCall(std::chrono::microseconds latency, Grpc::Status::GrpcStatus call_status,
                      ProcessorState::CallbackState callback_state,
                      envoy::config::core::v3::TrafficDirection traffic_direction);
  void setBytesSent(uint64_t bytes_sent) { bytes_sent_ = bytes_sent; }
  void setBytesReceived(uint64_t bytes_received) { bytes_received_ = bytes_received; }
  void setClusterInfo(absl::optional<Upstream::ClusterInfoConstSharedPtr> cluster_info) {
    if (cluster_info) {
      cluster_info_ = cluster_info.value();
    }
  }
  void setUpstreamHost(absl::optional<Upstream::HostDescriptionConstSharedPtr> upstream_host) {
    if (upstream_host) {
      upstream_host_ = upstream_host.value();
    }
  }

  uint64_t bytesSent() const { return bytes_sent_; }
  uint64_t bytesReceived() const { return bytes_received_; }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() const { return cluster_info_; }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const { return upstream_host_; }
  const GrpcCalls& grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction) const;
  const Envoy::ProtobufWkt::Struct& filterMetadata() const { return filter_metadata_; }

private:
  GrpcCalls& grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction);
  GrpcCalls decoding_processor_grpc_calls_;
  GrpcCalls encoding_processor_grpc_calls_;
  const Envoy::ProtobufWkt::Struct filter_metadata_;
  // The following stats are populated for ext_proc filters using Envoy gRPC only.
  // The bytes sent and received are for the entire stream.
  uint64_t bytes_sent_{0}, bytes_received_{0};
  Upstream::ClusterInfoConstSharedPtr cluster_info_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
};

// Changes to headers are normally tested against the MutationRules supplied
// with configuration. When writing an immediate response message, however,
// we want to support a more liberal set of rules so that filters can create
// custom error messages, and we want to prevent the MutationRules in the
// configuration from making that impossible. This is a fixed, permissive
// set of rules for that purpose.
class ImmediateMutationChecker {
public:
  ImmediateMutationChecker(Regex::Engine& regex_engine) {
    envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
    rules.mutable_allow_all_routing()->set_value(true);
    rules.mutable_allow_envoy()->set_value(true);
    rule_checker_ = std::make_unique<Filters::Common::MutationRules::Checker>(rules, regex_engine);
  }

  const Filters::Common::MutationRules::Checker& checker() const { return *rule_checker_; }

private:
  std::unique_ptr<Filters::Common::MutationRules::Checker> rule_checker_;
};

class ThreadLocalStreamManager : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  // Store the ExternalProcessorStreamPtr in the map and return its raw pointer.
  ExternalProcessorStream* store(Filter* filter, ExternalProcessorStreamPtr stream) {
    stream_manager_[filter] = std::move(stream);
    return stream_manager_[filter].get();
  }

  void erase(Filter* filter) { stream_manager_.erase(filter); }

private:
  // Map of ExternalProcessorStreamPtrs with filter pointer as key.
  absl::flat_hash_map<Filter*, ExternalProcessorStreamPtr> stream_manager_;
};

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config,
               const std::chrono::milliseconds message_timeout,
               const uint32_t max_message_timeout_ms, Stats::Scope& scope,
               const std::string& stats_prefix, bool is_upstream,
               Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder,
               Server::Configuration::CommonFactoryContext& context);

  bool failureModeAllow() const { return failure_mode_allow_; }

  const std::chrono::milliseconds& messageTimeout() const { return message_timeout_; }

  uint32_t maxMessageTimeout() const { return max_message_timeout_ms_; }

  const ExtProcFilterStats& stats() const { return stats_; }

  const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& processingMode() const {
    return processing_mode_;
  }

  bool allowModeOverride() const { return allow_mode_override_; }
  bool disableImmediateResponse() const { return disable_immediate_response_; }

  const Filters::Common::MutationRules::Checker& mutationChecker() const {
    return mutation_checker_;
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::RouteCacheAction
  routeCacheAction() const {
    return route_cache_action_;
  }

  const std::vector<Matchers::StringMatcherPtr>& allowedHeaders() const { return allowed_headers_; }
  const std::vector<Matchers::StringMatcherPtr>& disallowedHeaders() const {
    return disallowed_headers_;
  }

  const ProtobufWkt::Struct& filterMetadata() const { return filter_metadata_; }

  const ExpressionManager& expressionManager() const { return expression_manager_; }

  bool isUpstream() const { return is_upstream_; }

  const std::vector<std::string>& untypedForwardingMetadataNamespaces() const {
    return untyped_forwarding_namespaces_;
  }

  const std::vector<std::string>& typedForwardingMetadataNamespaces() const {
    return typed_forwarding_namespaces_;
  }

  const std::vector<std::string>& untypedReceivingMetadataNamespaces() const {
    return untyped_receiving_namespaces_;
  }

  const ImmediateMutationChecker& immediateMutationChecker() const {
    return immediate_mutation_checker_;
  }

  ThreadLocalStreamManager& threadLocalStreamManager() {
    return thread_local_stream_manager_slot_->getTyped<ThreadLocalStreamManager>();
  }

private:
  ExtProcFilterStats generateStats(const std::string& prefix,
                                   const std::string& filter_stats_prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "ext_proc.", filter_stats_prefix);
    return {ALL_EXT_PROC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }
  const bool failure_mode_allow_;
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor::RouteCacheAction
      route_cache_action_;
  const std::chrono::milliseconds message_timeout_;
  const uint32_t max_message_timeout_ms_;

  ExtProcFilterStats stats_;
  const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode processing_mode_;
  const Filters::Common::MutationRules::Checker mutation_checker_;
  const ProtobufWkt::Struct filter_metadata_;
  // If set to true, allow the processing mode to be modified by the ext_proc response.
  const bool allow_mode_override_;
  // If set to true, disable the immediate response from the ext_proc server, which means
  // closing the stream to the ext_proc server, and no more external processing.
  const bool disable_immediate_response_;
  // Empty allowed_header_ means allow all.
  const std::vector<Matchers::StringMatcherPtr> allowed_headers_;
  // Empty disallowed_header_ means disallow nothing, i.e, allow all.
  const std::vector<Matchers::StringMatcherPtr> disallowed_headers_;
  // is_upstream_ is true if ext_proc filter is in the upstream filter chain.
  const bool is_upstream_;
  const std::vector<std::string> untyped_forwarding_namespaces_;
  const std::vector<std::string> typed_forwarding_namespaces_;
  const std::vector<std::string> untyped_receiving_namespaces_;
  const ExpressionManager expression_manager_;

  const ImmediateMutationChecker immediate_mutation_checker_;
  ThreadLocal::SlotPtr thread_local_stream_manager_slot_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  explicit FilterConfigPerRoute(
      const envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute& config);

  // This constructor is used as a way to merge more-specific config into less-specific config in a
  // clearly defined way (e.g. route config into vh config). All fields on this class must be const
  // and thus must be initialized in the ctor initialization list.
  FilterConfigPerRoute(const FilterConfigPerRoute& less_specific,
                       const FilterConfigPerRoute& more_specific);

  bool disabled() const { return disabled_; }
  const absl::optional<const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode>&
  processingMode() const {
    return processing_mode_;
  }
  const absl::optional<const envoy::config::core::v3::GrpcService>& grpcService() const {
    return grpc_service_;
  }
  const std::vector<envoy::config::core::v3::HeaderValue>& grpcInitialMetadata() const {
    return grpc_initial_metadata_;
  }

  const absl::optional<const std::vector<std::string>>&
  untypedForwardingMetadataNamespaces() const {
    return untyped_forwarding_namespaces_;
  }
  const absl::optional<const std::vector<std::string>>& typedForwardingMetadataNamespaces() const {
    return typed_forwarding_namespaces_;
  }
  const absl::optional<const std::vector<std::string>>& untypedReceivingMetadataNamespaces() const {
    return untyped_receiving_namespaces_;
  }

private:
  const bool disabled_;
  const absl::optional<const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode>
      processing_mode_;
  const absl::optional<const envoy::config::core::v3::GrpcService> grpc_service_;
  std::vector<envoy::config::core::v3::HeaderValue> grpc_initial_metadata_;

  const absl::optional<const std::vector<std::string>> untyped_forwarding_namespaces_;
  const absl::optional<const std::vector<std::string>> typed_forwarding_namespaces_;
  const absl::optional<const std::vector<std::string>> untyped_receiving_namespaces_;
};

class Filter : public Logger::Loggable<Logger::Id::ext_proc>,
               public Http::PassThroughFilter,
               public ExternalProcessorCallbacks {
  // The result of an attempt to open the stream
  enum class StreamOpenState {
    // The stream was opened successfully
    Ok,
    // The stream was not opened successfully and an error was delivered
    // downstream -- processing should stop
    Error,
    // The stream was not opened successfully but processing should
    // continue as if the stream was already closed.
    IgnoreError,
  };

public:
  Filter(const FilterConfigSharedPtr& config, ExternalProcessorClientPtr&& client,
         const envoy::config::core::v3::GrpcService& grpc_service)
      : config_(config), client_(std::move(client)), stats_(config->stats()),
        grpc_service_(grpc_service), config_with_hash_key_(grpc_service),
        decoding_state_(*this, config->processingMode(),
                        config->untypedForwardingMetadataNamespaces(),
                        config->typedForwardingMetadataNamespaces(),
                        config->untypedReceivingMetadataNamespaces()),
        encoding_state_(*this, config->processingMode(),
                        config->untypedForwardingMetadataNamespaces(),
                        config->typedForwardingMetadataNamespaces(),
                        config->untypedReceivingMetadataNamespaces()) {}

  const FilterConfig& config() const { return *config_; }
  const envoy::config::core::v3::GrpcService& grpc_service_config() const {
    return config_with_hash_key_.config();
  }

  ExtProcFilterStats& stats() { return stats_; }
  ExtProcLoggingInfo* loggingInfo() { return logging_info_; }

  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  // ExternalProcessorCallbacks
  void onReceiveMessage(
      std::unique_ptr<envoy::service::ext_proc::v3::ProcessingResponse>&& response) override;
  void onGrpcError(Grpc::Status::GrpcStatus error) override;
  void onGrpcClose() override;
  void logGrpcStreamInfo() override;

  void onMessageTimeout();
  void onNewTimeout(const ProtobufWkt::Duration& override_message_timeout);

  envoy::service::ext_proc::v3::ProcessingRequest
  setupBodyChunk(ProcessorState& state, const Buffer::Instance& data, bool end_stream);
  void sendBodyChunk(ProcessorState& state, ProcessorState::CallbackState new_state,
                     envoy::service::ext_proc::v3::ProcessingRequest& req);

  void sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers);
  bool inHeaderProcessState() {
    return (decoding_state_.callbackState() == ProcessorState::CallbackState::HeadersCallback ||
            encoding_state_.callbackState() == ProcessorState::CallbackState::HeadersCallback);
  }

  const ProcessorState& encodingState() { return encoding_state_; }
  const ProcessorState& decodingState() { return decoding_state_; }

private:
  void mergePerRouteConfig();
  StreamOpenState openStream();
  void closeStream();

  void onFinishProcessorCalls(Grpc::Status::GrpcStatus call_status);
  void clearAsyncState();
  void sendImmediateResponse(const envoy::service::ext_proc::v3::ImmediateResponse& response);

  Http::FilterHeadersStatus onHeaders(ProcessorState& state,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream);

  // Return a pair of whether to terminate returning the current result.
  std::pair<bool, Http::FilterDataStatus> sendStreamChunk(ProcessorState& state);
  Http::FilterDataStatus onData(ProcessorState& state, Buffer::Instance& data, bool end_stream);
  Http::FilterTrailersStatus onTrailers(ProcessorState& state, Http::HeaderMap& trailers);
  void setDynamicMetadata(Http::StreamFilterCallbacks* cb, const ProcessorState& state,
                          const envoy::service::ext_proc::v3::ProcessingResponse& response);
  void setEncoderDynamicMetadata(const envoy::service::ext_proc::v3::ProcessingResponse& response);
  void setDecoderDynamicMetadata(const envoy::service::ext_proc::v3::ProcessingResponse& response);
  void addDynamicMetadata(const ProcessorState& state,
                          envoy::service::ext_proc::v3::ProcessingRequest& req);
  void addAttributes(ProcessorState& state, envoy::service::ext_proc::v3::ProcessingRequest& req);

  const FilterConfigSharedPtr config_;
  const ExternalProcessorClientPtr client_;
  ExtProcFilterStats stats_;
  ExtProcLoggingInfo* logging_info_;
  envoy::config::core::v3::GrpcService grpc_service_;
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key_;

  // The state of the filter on both the encoding and decoding side.
  DecodingProcessorState decoding_state_;
  EncodingProcessorState encoding_state_;

  // The gRPC stream to the external processor, which will be opened
  // when it's time to send the first message.
  ExternalProcessorStream* stream_ = nullptr;

  // Set to true when no more messages need to be sent to the processor.
  // This happens when the processor has closed the stream, or when it has
  // failed.
  bool processing_complete_ = false;

  // Set to true when an "immediate response" has been delivered. This helps us
  // know what response to return from certain failures.
  bool sent_immediate_response_ = false;

  // Set to true when the mergePerRouteConfig() method has been called.
  bool route_config_merged_ = false;

  std::vector<std::string> untyped_forwarding_namespaces_{};
  std::vector<std::string> typed_forwarding_namespaces_{};
  std::vector<std::string> untyped_receiving_namespaces_{};
};

extern std::string responseCaseToString(
    const envoy::service::ext_proc::v3::ProcessingResponse::ResponseCase response_case);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
