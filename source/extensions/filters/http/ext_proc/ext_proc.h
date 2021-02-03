#pragma once

#include <chrono>
#include <memory>

#include "envoy/extensions/filters/http/ext_proc/v3alpha/ext_proc.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/http/filter.h"
#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/ext_proc/client.h"

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
  COUNTER(failure_mode_allowed)

struct ExtProcFilterStats {
  ALL_EXT_PROC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& config,
               const std::chrono::milliseconds grpc_timeout, Stats::Scope& scope,
               const std::string& stats_prefix)
      : failure_mode_allow_(config.failure_mode_allow()), grpc_timeout_(grpc_timeout),
        stats_(generateStats(stats_prefix, config.stat_prefix(), scope)),
        processing_mode_(config.processing_mode()) {}

  bool failureModeAllow() const { return failure_mode_allow_; }

  const std::chrono::milliseconds& grpcTimeout() const { return grpc_timeout_; }

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
  const std::chrono::milliseconds grpc_timeout_;

  ExtProcFilterStats stats_;
  const envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode processing_mode_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class Filter : public Logger::Loggable<Logger::Id::filter>,
               public Http::PassThroughFilter,
               public ExternalProcessorCallbacks {
  // The state of filter execution -- this is used to determine
  // how to handle gRPC callbacks.
  enum class FilterState {
    // The filter is not waiting for anything, so any response on the
    // gRPC stream is spurious and will result in the filter closing
    // the stream.
    IDLE,
    // The filter is waiting for a "request_headers" or a "response_headers" message.
    // Any other response on the gRPC stream will be treated as spurious.
    HEADERS,
  };

public:
  Filter(const FilterConfigSharedPtr& config, ExternalProcessorClientPtr&& client)
      : config_(config), client_(std::move(client)), stats_(config->stats()),
        processing_mode_(config->processingMode()) {}

  void onDestroy() override;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

  // ExternalProcessorCallbacks

  void onReceiveMessage(
      std::unique_ptr<envoy::service::ext_proc::v3alpha::ProcessingResponse>&& response) override;

  void onGrpcError(Grpc::Status::GrpcStatus error) override;

  void onGrpcClose() override;

private:
  void openStream();
  void closeStream();
  void cleanupState();
  void sendImmediateResponse(const envoy::service::ext_proc::v3alpha::ImmediateResponse& response);

  bool
  handleRequestHeadersResponse(const envoy::service::ext_proc::v3alpha::HeadersResponse& response);
  bool
  handleResponseHeadersResponse(const envoy::service::ext_proc::v3alpha::HeadersResponse& response);
  void
  handleImmediateResponse(const envoy::service::ext_proc::v3alpha::ImmediateResponse& response);

  const FilterConfigSharedPtr config_;
  const ExternalProcessorClientPtr client_;
  ExtProcFilterStats stats_;

  // The state of the request-processing, or "decoding" side of the filter.
  // We maintain separate states for encoding and decoding since they may
  // be interleaved.
  FilterState request_state_ = FilterState::IDLE;

  // The state of the response-processing side
  FilterState response_state_ = FilterState::IDLE;

  // The gRPC stream to the external processor, which will be opened
  // when it's time to send the first message.
  ExternalProcessorStreamPtr stream_;

  // Set to true when the stream has been closed and no more messages
  // need to be sent to the processor. This happens when the processor
  // has closed the stream, or when it has failed, or when the filter
  // is destroyed.
  bool processing_complete_ = false;

  Http::HeaderMap* request_headers_ = nullptr;
  Http::HeaderMap* response_headers_ = nullptr;

  // The processing mode. May be locally overridden by any response,
  // So every instance of the filter has a copy.
  envoy::extensions::filters::http::ext_proc::v3alpha::ProcessingMode processing_mode_;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy