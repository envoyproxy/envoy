#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
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
  COUNTER(message_timeouts)

struct ExtProcFilterStats {
  ALL_EXT_PROC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& config,
               const std::chrono::milliseconds message_timeout, Stats::Scope& scope,
               const std::string& stats_prefix)
      : failure_mode_allow_(config.failure_mode_allow()), message_timeout_(message_timeout),
        stats_(generateStats(stats_prefix, config.stat_prefix(), scope)),
        processing_mode_(config.processing_mode()) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

  const std::chrono::milliseconds& messageTimeout() const { return message_timeout_; }

  const ExtProcFilterStats& stats() const { return stats_; }

  const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode&
  processingMode() const {
    return processing_mode_;
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
  const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode processing_mode_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Logger::Loggable<Logger::Id::filter>,
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
  Filter(const FilterConfigSharedPtr& config, ExternalProcessorClientPtr&& client)
      : config_(config), client_(std::move(client)), stats_(config->stats()),
        decoding_state_(*this, config->processingMode()),
        encoding_state_(*this, config->processingMode()) {}

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
      std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& response) override;

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
  StreamOpenState openStream();

  void cleanUpTimers();
  void clearAsyncState();
  void sendImmediateResponse(const envoy::service::ext_proc::v3alpha::ImmediateResponse& response);

  Http::FilterHeadersStatus onHeaders(ProcessorState& state,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream);
  Http::FilterDataStatus onData(ProcessorState& state, Buffer::Instance& data, bool end_stream);
  Http::FilterTrailersStatus onTrailers(ProcessorState& state, Http::HeaderMap& trailers);

  const FilterConfigSharedPtr config_;
  const ExternalProcessorClientPtr client_;
  ExtProcFilterStats stats_;

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
    const envoy::service::ext_proc::v3alpha::ProcessingResponse::ResponseCase response_case);

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
