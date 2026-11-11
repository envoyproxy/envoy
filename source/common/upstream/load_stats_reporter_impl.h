#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/config/endpoint/v3/load_report.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/load_stats_reporter.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/grpc/typed_async_client.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

/**
 * Implementation of LoadStatsReporter that streams load reports to a management server via gRPC.
 *
 * The reporter sends load statistics for clusters as directed by the management server.
 * The frequency of reports is determined by the load_reporting_interval in the LoadStatsResponse.
 *
 * By default, load reports for a locality are sent if the sum of the latched `rq_total_` values
 * or the sum of the current `rq_active_` values for hosts in the locality is non-zero during the
 * reporting interval. Reporting on non-zero `rq_active_` is needed to report long-lived
 * connections/requests (e.g., when web-sockets are used).
 *
 * The following runtime feature controls the behavior of the load reporter:
 * - envoy.reloadable_features.report_load_for_non_zero_stats: If true, load reports for a
 *   locality are sent if any of the following conditions are met for the sum of host stats in that
 *   locality:
 *     - Latched `rq_success_` is non-zero.
 *     - Latched `rq_error_` is non-zero.
 *     - Current `rq_active_` is non-zero.
 *     - Latched `rq_total_` is non-zero.
 *     - Any custom load metrics are non-zero in `LoadMetricStats`.
 * - envoy.reloadable_features.lrs_skip_idle_clusters: If true, the hosts of a cluster are only
 *   visited (and their stats latched) when the cluster's
 *   `ClusterLoadReportStats::upstream_host_load_updates_` counter moved since the last report or
 *   the last visit found requests in progress. This does not change what is reported: a cluster
 *   that is skipped would not have contributed any locality stats. It merely avoids walking the
 *   hosts of idle clusters, which dominates the cost of load reporting with many clusters.
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
      Grpc::ResponsePtr<envoy::service::load_stats::v3::LoadStatsResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

  // Upstream::LoadStatsReporter
  const LoadReporterStats& getStats() const override { return stats_; };

  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;

private:
  // Per-cluster state tracked between load reports.
  struct ClusterLoadReportState {
    explicit ClusterLoadReportState(std::chrono::steady_clock::duration interval_start)
        : interval_start(interval_start) {}

    // Start of the current measurement interval.
    std::chrono::steady_clock::duration interval_start;
    // Whether the last visit of the cluster's hosts found requests in progress. Such clusters are
    // visited again on the next report so that total_requests_in_progress keeps being reported.
    // Starts out true so that a newly tracked cluster is always visited at least once.
    bool requests_in_progress{true};
    // Generation stamp assigned by startLoadReportPeriod(). Entries that were not (re)selected in
    // the current generation are no longer tracked.
    uint64_t generation{0};
  };

  void setRetryTimer();
  void establishNewStream();
  void sendLoadStatsRequest();
  void handleFailure();
  void startLoadReportPeriod();
  // Appends the ClusterStats of a tracked cluster to request and starts its next measurement
  // interval.
  void addClusterStats(envoy::service::load_stats::v3::LoadStatsRequest& request,
                       const std::string& cluster_name, const Cluster& cluster,
                       ClusterLoadReportState& state, bool skip_idle_clusters,
                       bool report_load_for_non_zero_stats);
  // Latches the load stats of all hosts of cluster and appends the resulting locality (and
  // optionally endpoint) stats to cluster_stats.
  // @return whether any host of the cluster has requests in progress.
  bool addLocalityStats(envoy::config::endpoint::v3::ClusterStats& cluster_stats,
                        const Cluster& cluster, bool report_load_for_non_zero_stats);
  // Marks cluster_name as selected for the current generation and, if it was not tracked yet,
  // starts tracking it with a measurement interval starting now.
  // @return whether cluster_name was not tracked before.
  bool trackCluster(absl::string_view cluster_name, std::chrono::steady_clock::duration now);

  ClusterManager& cm_;
  LoadReporterStats stats_;
  Grpc::AsyncClient<envoy::service::load_stats::v3::LoadStatsRequest,
                    envoy::service::load_stats::v3::LoadStatsResponse>
      async_client_;
  Grpc::AsyncStream<envoy::service::load_stats::v3::LoadStatsRequest> stream_;
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  const envoy::service::load_stats::v3::LoadStatsRequest request_template_;
  Grpc::ResponsePtr<envoy::service::load_stats::v3::LoadStatsResponse> message_{nullptr};
  // Map from cluster name to the load reporting state of the cluster. Only holds clusters selected
  // by the latest LoadStatsResponse.
  absl::flat_hash_map<std::string, ClusterLoadReportState> clusters_;
  // Generation of the latest startLoadReportPeriod() call, see ClusterLoadReportState::generation.
  uint64_t generation_{0};
  TimeSource& time_source_;
};

} // namespace Upstream
} // namespace Envoy
