#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/token_bucket_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/common/fault/fault_config.h"
#include "source/extensions/filters/http/common/stream_rate_limiter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * All stats for the fault filter. @see stats_macros.h
 */
#define ALL_FAULT_FILTER_STATS(COUNTER, GAUGE)                                                     \
  COUNTER(aborts_injected)                                                                         \
  COUNTER(delays_injected)                                                                         \
  COUNTER(faults_overflow)                                                                         \
  COUNTER(response_rl_injected)                                                                    \
  GAUGE(active_faults, Accumulate)

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct FaultFilterStats {
  ALL_FAULT_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for fault injection.
 */
class FaultSettings : public Router::RouteSpecificFilterConfig {
public:
  FaultSettings(const envoy::extensions::filters::http::fault::v3::HTTPFault& fault,
                Server::Configuration::CommonFactoryContext& context);

  const std::vector<Http::HeaderUtility::HeaderDataPtr>& filterHeaders() const {
    return fault_filter_headers_;
  }
  const Filters::Common::Fault::FaultAbortConfig* requestAbort() const {
    return request_abort_config_.get();
  }
  const Filters::Common::Fault::FaultDelayConfig* requestDelay() const {
    return request_delay_config_.get();
  }
  const std::string& upstreamCluster() const { return upstream_cluster_; }
  const absl::flat_hash_set<std::string>& downstreamNodes() const { return downstream_nodes_; }
  absl::optional<uint64_t> maxActiveFaults() const { return max_active_faults_; }
  const Filters::Common::Fault::FaultRateLimitConfig* responseRateLimit() const {
    return response_rate_limit_.get();
  }
  const std::string& abortPercentRuntime() const { return abort_percent_runtime_; }
  const std::string& delayPercentRuntime() const { return delay_percent_runtime_; }
  const std::string& abortHttpStatusRuntime() const { return abort_http_status_runtime_; }
  const std::string& abortGrpcStatusRuntime() const { return abort_grpc_status_runtime_; }
  const std::string& delayDurationRuntime() const { return delay_duration_runtime_; }
  const std::string& maxActiveFaultsRuntime() const { return max_active_faults_runtime_; }
  const std::string& responseRateLimitPercentRuntime() const {
    return response_rate_limit_percent_runtime_;
  }
  bool disableDownstreamClusterStats() const { return disable_downstream_cluster_stats_; }
  const Envoy::ProtobufWkt::Struct& filterMetadata() const { return filter_metadata_; }

private:
  class RuntimeKeyValues {
  public:
    const std::string DelayPercentKey = "fault.http.delay.fixed_delay_percent";
    const std::string AbortPercentKey = "fault.http.abort.abort_percent";
    const std::string DelayDurationKey = "fault.http.delay.fixed_duration_ms";
    const std::string AbortHttpStatusKey = "fault.http.abort.http_status";
    const std::string AbortGrpcStatusKey = "fault.http.abort.grpc_status";
    const std::string MaxActiveFaultsKey = "fault.http.max_active_faults";
    const std::string ResponseRateLimitPercentKey = "fault.http.rate_limit.response_percent";
  };

  using RuntimeKeys = ConstSingleton<RuntimeKeyValues>;

  envoy::type::v3::FractionalPercent abort_percentage_;
  Filters::Common::Fault::FaultDelayConfigPtr request_delay_config_;
  Filters::Common::Fault::FaultAbortConfigPtr request_abort_config_;
  std::string upstream_cluster_; // restrict faults to specific upstream cluster
  const std::vector<Http::HeaderUtility::HeaderDataPtr> fault_filter_headers_;
  absl::flat_hash_set<std::string> downstream_nodes_{}; // Inject failures for specific downstream
  absl::optional<uint64_t> max_active_faults_;

  Filters::Common::Fault::FaultRateLimitConfigPtr response_rate_limit_;
  const std::string delay_percent_runtime_;
  const std::string abort_percent_runtime_;
  const std::string delay_duration_runtime_;
  const std::string abort_http_status_runtime_;
  const std::string abort_grpc_status_runtime_;
  const std::string max_active_faults_runtime_;
  const std::string response_rate_limit_percent_runtime_;
  const bool disable_downstream_cluster_stats_;

  const Envoy::ProtobufWkt::Struct filter_metadata_;
};

/**
 * Configuration for the fault filter.
 */
class FaultFilterConfig {
public:
  FaultFilterConfig(const envoy::extensions::filters::http::fault::v3::HTTPFault& fault,
                    const std::string& stats_prefix, Stats::Scope& scope,
                    Server::Configuration::CommonFactoryContext& context);

  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }
  Stats::Scope& scope() { return scope_; }
  const FaultSettings* settings() { return &settings_; }
  TimeSource& timeSource() { return time_source_; }

  void incDelays(Stats::StatName downstream_cluster) {
    incCounter(downstream_cluster, delays_injected_);
  }

  void incAborts(Stats::StatName downstream_cluster) {
    incCounter(downstream_cluster, aborts_injected_);
  }

private:
  static FaultFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);
  void incCounter(Stats::StatName downstream_cluster, Stats::StatName stat_name);

  const FaultSettings settings_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
  Stats::Scope& scope_;
  TimeSource& time_source_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName aborts_injected_;
  const Stats::StatName delays_injected_;
  const Stats::StatName stats_prefix_; // Includes ".fault".
};

using FaultFilterConfigSharedPtr = std::shared_ptr<FaultFilterConfig>;

using AbortHttpAndGrpcStatus =
    std::pair<absl::optional<Http::Code>, absl::optional<Grpc::Status::GrpcStatus>>;
/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  FaultFilter(FaultFilterConfigSharedPtr config);
  ~FaultFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  bool faultOverflow();
  void recordAbortsInjectedStats();
  void recordDelaysInjectedStats();
  void resetTimerState();
  void postDelayInjection(const Http::RequestHeaderMap& request_headers);
  void abortWithStatus(Http::Code http_status_code,
                       absl::optional<Grpc::Status::GrpcStatus> grpc_status_code);
  bool matchesTargetUpstreamCluster();
  bool matchesDownstreamNodes(const Http::RequestHeaderMap& headers);
  bool isAbortEnabled(const Http::RequestHeaderMap& request_headers);
  bool isDelayEnabled(const Http::RequestHeaderMap& request_headers);
  bool isResponseRateLimitEnabled(const Http::RequestHeaderMap& request_headers);
  bool isResponseRateLimitConfigured();
  absl::optional<std::chrono::milliseconds>
  delayDuration(const Http::RequestHeaderMap& request_headers);
  AbortHttpAndGrpcStatus abortStatus(const Http::RequestHeaderMap& request_headers);
  absl::optional<Http::Code> abortHttpStatus(const Http::RequestHeaderMap& request_headers);
  absl::optional<Grpc::Status::GrpcStatus>
  abortGrpcStatus(const Http::RequestHeaderMap& request_headers);
  // Attempts to increase the number of active faults. Returns false if we've reached the maximum
  // number of allowed faults, in which case no fault should be performed.
  bool tryIncActiveFaults();
  bool maybeDoAbort(const Http::RequestHeaderMap& request_headers);
  bool maybeSetupDelay(const Http::RequestHeaderMap& request_headers);
  void maybeSetupResponseRateLimit(const Http::RequestHeaderMap& request_headers);

  FaultFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Event::TimerPtr delay_timer_;
  std::string downstream_cluster_{};
  std::unique_ptr<Stats::StatNameDynamicStorage> downstream_cluster_storage_;
  const FaultSettings* fault_settings_;
  bool fault_active_{};
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> response_limiter_;
  std::string downstream_cluster_delay_percent_key_{};
  std::string downstream_cluster_abort_percent_key_{};
  std::string downstream_cluster_delay_duration_key_{};
  std::string downstream_cluster_abort_http_status_key_{};
  std::string downstream_cluster_abort_grpc_status_key_{};
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
