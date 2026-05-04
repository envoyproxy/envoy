#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/load_stats_reporter.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/grpc/typed_async_client.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of LoadStatsReporter that streams load reports to a management server via gRPC.
 *
 * The reporter sends load statistics for clusters as directed by the management server.
 * The frequency of reports is determined by the load_reporting_interval in the LoadStatsResponse.
 *
 * By default, if no runtime flags are set, load reports for a locality are sent only if the
 * sum of `rq_total_` latched values for hosts in the locality is non-zero during the reporting
 * interval.
 *
 * The following runtime features control the behavior of the load reporter:
 * - envoy.reloadable_features.report_load_when_rq_active_is_non_zero: If true, load reports
 *   for a locality are sent if the sum of `rq_active_` values for hosts in the locality is
 * non-zero, even if no new requests were issued in the interval.
 * - envoy.reloadable_features.report_load_for_non_zero_stats: If true, load reports for a
 *   locality are sent if any of the following conditions are met for the sum of host stats in that
 *   locality:
 *     - Latched `rq_success_` is non-zero.
 *     - Latched `rq_error_` is non-zero.
 *     - Current `rq_active_` is non-zero.
 *     - Latched `rq_total_` is non-zero.
 *     - Any custom load metrics are non-zero in `LoadMetricStats`.
 *
 * Only one of these runtime features should be enabled at a time.
 */
class LoadStatsReporterImpl
    : public LoadStatsReporter,
      public Grpc::AsyncStreamCallbacks<envoy::service::load_stats::v3::LoadStatsResponse>,
      public Logger::Loggable<Logger::Id::upstream> {
public:
  LoadStatsReporterImpl(const LocalInfo::LocalInfo& local_info, ClusterManager& cluster_manager,
                        Stats::Scope& scope, Grpc::RawAsyncClientSharedPtr async_client,
                        Event::Dispatcher& dispatcher);
  ~LoadStatsReporterImpl() override;

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // Upstream::LoadStatsReporter
  const LoadReporterStats& getStats() const override { return stats_; };

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  void setRetryTimer();
  void establishNewStream();
  void sendLoadStatsRequest();
  void handleFailure();
  void startLoadReportPeriod();

  ClusterManager& cm_;
  LoadReporterStats stats_;
  Grpc::AsyncClient<envoy::service::load_stats::v3::LoadStatsRequest,
                    envoy::service::load_stats::v3::LoadStatsResponse>
      async_client_;
  Grpc::AsyncStream<envoy::service::load_stats::v3::LoadStatsRequest> stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  const envoy::service::load_stats::v3::LoadStatsRequest request_template_;
  std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse> message_;
  // Map from cluster name to start of measurement interval.
  absl::node_hash_map<std::string, std::chrono::steady_clock::duration> clusters_;
  TimeSource& time_source_;
};

} // namespace Upstream
} // namespace Envoy
