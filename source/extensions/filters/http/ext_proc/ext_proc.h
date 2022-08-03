#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/ext_proc/client.h"
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
  COUNTER(rejected_header_mutations)

struct ExtProcFilterStats {
  ALL_EXT_PROC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

inline constexpr absl::string_view ExtProcLoggingInfoName = "ext-proc-logging-info";

class ExtProcLoggingInfo : public Envoy::StreamInfo::FilterState::Object {
public:
  struct GrpcCall {
    GrpcCall(const std::chrono::microseconds latency, const Grpc::Status::GrpcStatus status,
             const ProcessorState::CallbackState callback_state)
        : latency_(latency), status_(status), callback_state_(callback_state) {}
    const std::chrono::microseconds latency_;
    const Grpc::Status::GrpcStatus status_;
    const ProcessorState::CallbackState callback_state_;
  };
  using GrpcCalls = std::vector<GrpcCall>;

  void recordGrpcCall(std::chrono::microseconds latency, Grpc::Status::GrpcStatus call_status,
                      ProcessorState::CallbackState callback_state,
                      envoy::config::core::v3::TrafficDirection traffic_direction);

  const GrpcCalls& grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction) const;

private:
  GrpcCalls& grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction);
  GrpcCalls decoding_processor_grpc_calls_;
  GrpcCalls encoding_processor_grpc_calls_;
};

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& config,
               const std::chrono::milliseconds message_timeout, Stats::Scope& scope,
               const std::string& stats_prefix)
      : failure_mode_allow_(config.failure_mode_allow()), message_timeout_(message_timeout),
        stats_(generateStats(stats_prefix, config.stat_prefix(), scope)),
        processing_mode_(config.processing_mode()), mutation_checker_(config.mutation_rules()) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

  const std::chrono::milliseconds& messageTimeout() const { return message_timeout_; }

  const ExtProcFilterStats& stats() const { return stats_; }

  const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode& processingMode() const {
    return processing_mode_;
  }

  const Filters::Common::MutationRules::Checker& mutationChecker() const {
    return mutation_checker_;
  }

private:
  ExtProcFilterStats generateStats(const std::string& prefix,
                                   const std::string& filter_stats_prefix, Stats::Scope& scope) {
    const std::string final_prefix = absl::StrCat(prefix, "ext_proc.", filter_stats_prefix);
    return {ALL_EXT_PROC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  const bool failure_mode_allow_;
  const std::chrono::milliseconds message_timeout_;

  ExtProcFilterStats stats_;
  const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode processing_mode_;
  const Filters::Common::MutationRules::Checker mutation_checker_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  explicit FilterConfigPerRoute(
      const envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute& config);

  void merge(const FilterConfigPerRoute& other);

  bool disabled() const { return disabled_; }
  const absl::optional<envoy::extensions::filters::http::ext_proc::v3::ProcessingMode>&
  processingMode() const {
    return processing_mode_;
  }
  const absl::optional<envoy::config::core::v3::GrpcService>& grpcService() const {
    return grpc_service_;
  }

private:
  bool disabled_;
  absl::optional<envoy::extensions::filters::http::ext_proc::v3::ProcessingMode> processing_mode_;
  absl::optional<envoy::config::core::v3::GrpcService> grpc_service_;
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
        grpc_service_(grpc_service), decoding_state_(*this, config->processingMode()),
        encoding_state_(*this, config->processingMode()) {}

  const FilterConfig& config() const { return *config_; }

  ExtProcFilterStats& stats() { return stats_; }
  ExtProcLoggingInfo& loggingInfo() { return *logging_info_; }

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

  void onMessageTimeout();

  void sendBufferedData(ProcessorState& state, ProcessorState::CallbackState new_state,
                        bool end_stream) {
    sendBodyChunk(state, *state.bufferedData(), new_state, end_stream);
  }
  void sendBodyChunk(ProcessorState& state, const Buffer::Instance& data,
                     ProcessorState::CallbackState new_state, bool end_stream);

  void sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers);

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
  std::pair<bool, Http::FilterDataStatus> sendStreamChunk(ProcessorState& state,
                                                          Buffer::Instance& data, bool end_stream);
  Http::FilterDataStatus onData(ProcessorState& state, Buffer::Instance& data, bool end_stream);
  Http::FilterTrailersStatus onTrailers(ProcessorState& state, Http::HeaderMap& trailers);

  const FilterConfigSharedPtr config_;
  const ExternalProcessorClientPtr client_;
  ExtProcFilterStats stats_;
  ExtProcLoggingInfo* logging_info_;
  envoy::config::core::v3::GrpcService grpc_service_;

  // The state of the filter on both the encoding and decoding side.
  DecodingProcessorState decoding_state_;
  EncodingProcessorState encoding_state_;

  // The gRPC stream to the external processor, which will be opened
  // when it's time to send the first message.
  ExternalProcessorStreamPtr stream_;

  // Set to true when no more messages need to be sent to the processor.
  // This happens when the processor has closed the stream, or when it has
  // failed.
  bool processing_complete_ = false;

  // Set to true when an "immediate response" has been delivered. This helps us
  // know what response to return from certain failures.
  bool sent_immediate_response_ = false;
};

extern std::string responseCaseToString(
    const envoy::service::ext_proc::v3::ProcessingResponse::ResponseCase response_case);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
