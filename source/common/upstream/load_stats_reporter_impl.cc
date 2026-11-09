#include "source/common/upstream/load_stats_reporter_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/optref.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

namespace {

envoy::service::load_stats::v3::LoadStatsRequest
// NOLINTNEXTLINE(readability-identifier-naming)
MakeRequestTemplate(const LocalInfo::LocalInfo& local_info) {
  envoy::service::load_stats::v3::LoadStatsRequest request;
  request.mutable_node()->MergeFrom(local_info.node());
  request.mutable_node()->add_client_features("envoy.lrs.supports_send_all_clusters");
  return request;
}

// Latches the load stats of a newly tracked cluster and of all its hosts, so that only load
// incurred from now on is reported for it. Like in addClusterStats(), the host load update counter
// is latched before the host stats, so that an update racing with this is observed by the next
// latch of the counter.
//
// The custom load metrics of the hosts (HostDescription::loadMetricStats()) are deliberately not
// latched, which preserves the long-standing behavior: metrics recorded before the cluster was
// tracked are reported with the first report that visits the host, on their own if
// envoy.reloadable_features.report_load_for_non_zero_stats is enabled and together with the next
// request stats otherwise. Should they need to be discarded as well, add
// host->loadMetricStats().latch() to the loop below and drop the returned map; latch() just moves
// the map out from under the host's mutex, so this would add little to the cost of tracking a
// cluster. LoadStatsReporterImplTest.NewlyTrackedClusterStatsAreLatched* pin the current behavior.
void latchClusterLoadStats(const Cluster& cluster) {
  ClusterLoadReportStats& load_report_stats = cluster.info()->loadReportStats();
  load_report_stats.upstream_host_load_updates_.latch();
  for (const HostSetPtr& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    for (const HostSharedPtr& host : host_set->hosts()) {
      host->stats().rq_success_.latch();
      host->stats().rq_error_.latch();
      host->stats().rq_total_.latch();
    }
  }
  load_report_stats.upstream_rq_dropped_.latch();
  load_report_stats.upstream_rq_drop_overload_.latch();
}

} // namespace

LoadStatsReporterImpl::LoadStatsReporterImpl(const LocalInfo::LocalInfo& local_info,
                                             ClusterManager& cluster_manager, Stats::Scope& scope,
                                             Grpc::RawAsyncClientSharedPtr async_client,
                                             Event::Dispatcher& dispatcher)
    : cm_(cluster_manager),
      stats_{ALL_LOAD_REPORTER_STATS(POOL_COUNTER_PREFIX(scope, "load_reporter."))},
      async_client_(std::move(async_client)),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.load_stats.v3.LoadReportingService.StreamLoadStats")),
      request_template_(MakeRequestTemplate(local_info)), time_source_(dispatcher.timeSource()) {
  retry_timer_ = dispatcher.createTimer([this]() -> void {
    stats_.retries_.inc();
    establishNewStream();
  });
  response_timer_ = dispatcher.createTimer([this]() -> void { sendLoadStatsRequest(); });
  establishNewStream();
}

LoadStatsReporterImpl::~LoadStatsReporterImpl() {
  // Disable the timer.
  ENVOY_LOG_MISC(info, "Destroying LoadStatsReporterImpl");
  retry_timer_->disableTimer();
  response_timer_->disableTimer();
  if (stream_ != nullptr) {
    stream_->resetStream();
    stream_ = nullptr;
  }
}

void LoadStatsReporterImpl::setRetryTimer() {
  ENVOY_LOG(info, "Load reporter stats stream/connection will retry in {} ms.", RETRY_DELAY_MS);
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void LoadStatsReporterImpl::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  sendLoadStatsRequest();
}

void LoadStatsReporterImpl::sendLoadStatsRequest() {
  // TODO(htuch): This sends load reports for only the set of clusters in clusters_, which
  // was initialized in startLoadReportPeriod() the last time we either sent a load report
  // or received a new LRS response (whichever happened more recently). The code in
  // startLoadReportPeriod() adds to clusters_ only those clusters that exist in the
  // ClusterManager at the moment when startLoadReportPeriod() runs. This means that if
  // a cluster is selected by the LRS server (either by being explicitly listed or by using
  // the send_all_clusters field), if that cluster was added to the ClusterManager since the
  // last time startLoadReportPeriod() was invoked, we will not report its load here. In
  // practice, this means that for any newly created cluster, we will always drop the data for
  // the initial load report period. This seems sub-optimal.
  //
  // One possible way to deal with this would be to get a notification whenever a new cluster is
  // added to the cluster manager. When we get the notification, we record the current time in
  // clusters_ as the start time for the load reporting window for that cluster.
  Envoy::Protobuf::Arena arena;
  auto* request =
      Envoy::Protobuf::Arena::Create<envoy::service::load_stats::v3::LoadStatsRequest>(&arena);
  request->MergeFrom(request_template_);
  // Runtime features are evaluated once per report rather than once per cluster or host.
  const bool skip_idle_clusters =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.lrs_skip_idle_clusters");
  const bool report_load_for_non_zero_stats =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.report_load_for_non_zero_stats");
  if (message_ != nullptr && message_->send_all_clusters()) {
    // Walk the cluster manager's active clusters instead of looking every tracked cluster up by
    // name. Active clusters that are not tracked yet are picked up by startLoadReportPeriod().
    cm_.forEachActiveCluster([&](const Cluster& cluster) {
      const auto it = clusters_.find(cluster.info()->name());
      if (it == clusters_.end()) {
        return;
      }
      addClusterStats(*request, it->first, cluster, it->second, skip_idle_clusters,
                      report_load_for_non_zero_stats);
    });
  } else {
    for (auto& [cluster_name, state] : clusters_) {
      OptRef<const Upstream::Cluster> active_cluster = cm_.getActiveCluster(cluster_name);
      if (!active_cluster.has_value()) {
        ENVOY_LOG(debug, "Cluster {} does not exist", cluster_name);
        continue;
      }
      addClusterStats(*request, cluster_name, *active_cluster, state, skip_idle_clusters,
                      report_load_for_non_zero_stats);
    }
  }

  ENVOY_LOG(trace, "Sending LoadStatsRequest: {}", request->DebugString());
  stream_->sendMessage(*request, false);
  stats_.responses_.inc();
  // When the connection is established, the message has not yet been read so we will not have a
  // load reporting period.
  if (message_) {
    startLoadReportPeriod();
  }
}

void LoadStatsReporterImpl::addClusterStats(
    envoy::service::load_stats::v3::LoadStatsRequest& request, const std::string& cluster_name,
    const Cluster& cluster, ClusterLoadReportState& state, bool skip_idle_clusters,
    bool report_load_for_non_zero_stats) {
  auto* cluster_stats = request.add_cluster_stats();
  cluster_stats->set_cluster_name(cluster_name);
  const ClusterInfoConstSharedPtr cluster_info = cluster.info();
  if (const auto& name = cluster_info->edsServiceName(); !name.empty()) {
    cluster_stats->set_cluster_service_name(name);
  }
  ClusterLoadReportStats& load_report_stats = cluster_info->loadReportStats();
  // Latch the host load update counter before visiting the hosts. Writers update the host stats
  // before incrementing the counter, so any update that this visit does not observe is observed by
  // the next latch of the counter: skipping a cluster never loses an update, it is at most
  // reported one period later.
  const bool hosts_updated = load_report_stats.upstream_host_load_updates_.latch() != 0;
  if (!skip_idle_clusters || hosts_updated || state.requests_in_progress) {
    state.requests_in_progress =
        addLocalityStats(*cluster_stats, cluster, report_load_for_non_zero_stats);
  }
  cluster_stats->set_total_dropped_requests(load_report_stats.upstream_rq_dropped_.latch());
  const uint64_t drop_overload_count = load_report_stats.upstream_rq_drop_overload_.latch();
  if (drop_overload_count > 0) {
    auto* dropped_request = cluster_stats->add_dropped_requests();
    dropped_request->set_category(cluster.dropCategory());
    dropped_request->set_dropped_count(drop_overload_count);
  }

  const auto now = time_source_.monotonicTime().time_since_epoch();
  const auto measured_interval = now - state.interval_start;
  cluster_stats->mutable_load_report_interval()->MergeFrom(
      Protobuf::util::TimeUtil::MicrosecondsToDuration(
          std::chrono::duration_cast<std::chrono::microseconds>(measured_interval).count()));
  state.interval_start = now;
}

bool LoadStatsReporterImpl::addLocalityStats(
    envoy::config::endpoint::v3::ClusterStats& cluster_stats, const Cluster& cluster,
    bool report_load_for_non_zero_stats) {
  bool requests_in_progress = false;
  for (const HostSetPtr& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    ENVOY_LOG(trace, "Load report locality count {}", host_set->hostsPerLocality().get().size());
    for (const HostVector& hosts : host_set->hostsPerLocality().get()) {
      if (hosts.empty()) {
        continue;
      }
      uint64_t rq_success = 0;
      uint64_t rq_error = 0;
      uint64_t rq_active = 0;
      uint64_t rq_issued = 0;
      LoadMetricStats::StatMap aggregated_host_custom_metrics;

      // This message is removed if no upstream locality stats need to be sent.
      auto* locality_stats = cluster_stats.add_upstream_locality_stats();
      locality_stats->mutable_locality()->MergeFrom(hosts[0]->locality());
      locality_stats->set_priority(host_set->priority());

      for (const HostSharedPtr& host : hosts) {
        uint64_t host_rq_success = host->stats().rq_success_.latch();
        uint64_t host_rq_error = host->stats().rq_error_.latch();
        uint64_t host_rq_active = host->stats().rq_active_.value();
        uint64_t host_rq_issued = host->stats().rq_total_.latch();

        // Check if the host has any load stats updates. If the host has no load stats updates, we
        // skip it.
        bool endpoint_has_updates =
            (host_rq_success + host_rq_error + host_rq_active + host_rq_issued) != 0;

        std::unique_ptr<LoadMetricStats::StatMap> host_custom_metrics;
        if (report_load_for_non_zero_stats) {
          host_custom_metrics = host->loadMetricStats().latch();
          if (host_custom_metrics != nullptr) {
            endpoint_has_updates = true;
          }
        }

        if (endpoint_has_updates) {
          rq_success += host_rq_success;
          rq_error += host_rq_error;
          rq_active += host_rq_active;
          rq_issued += host_rq_issued;

          envoy::config::endpoint::v3::UpstreamEndpointStats* upstream_endpoint_stats = nullptr;
          // Set the upstream endpoint stats if we are reporting endpoint granularity.
          if (message_ && message_->report_endpoint_granularity()) {
            upstream_endpoint_stats = locality_stats->add_upstream_endpoint_stats();
            Network::Utility::addressToProtobufAddress(*host->address(),
                                                       *upstream_endpoint_stats->mutable_address());
            upstream_endpoint_stats->set_total_successful_requests(host_rq_success);
            upstream_endpoint_stats->set_total_error_requests(host_rq_error);
            upstream_endpoint_stats->set_total_requests_in_progress(host_rq_active);
            upstream_endpoint_stats->set_total_issued_requests(host_rq_issued);
          }

          // TODO(fcfort): Remove this latch() call when cleaning up
          // `report_load_for_non_zero_stats`.
          if (host_custom_metrics == nullptr) {
            host_custom_metrics = host->loadMetricStats().latch();
          }
          if (host_custom_metrics != nullptr) {
            for (const auto& metric : *host_custom_metrics) {
              const auto& metric_name = metric.first;
              const auto& metric_value = metric.second;

              // Add the metric to the load metrics map.
              LoadMetricStats::Stat& stat = aggregated_host_custom_metrics[metric_name];
              stat.num_requests_with_metric += metric_value.num_requests_with_metric;
              stat.total_metric_value += metric_value.total_metric_value;

              // If we are reporting endpoint granularity, add the metric to the upstream endpoint
              // stats.
              if (upstream_endpoint_stats != nullptr) {
                auto* endpoint_load_metric = upstream_endpoint_stats->add_load_metric_stats();
                endpoint_load_metric->set_metric_name(metric_name);
                endpoint_load_metric->set_num_requests_finished_with_metric(
                    metric_value.num_requests_with_metric);
                endpoint_load_metric->set_total_metric_value(metric_value.total_metric_value);
              }
            }
          }
        }
      }

      // Every host with requests in progress has updates, so this is the sum over all hosts.
      if (rq_active != 0) {
        requests_in_progress = true;
      }

      // Upstream locality stats

      bool should_send_locality_stats = false;
      if (report_load_for_non_zero_stats) {
        bool has_host_custom_metrics = false;
        for (const auto& metric : aggregated_host_custom_metrics) {
          if (metric.second.num_requests_with_metric != 0 ||
              metric.second.total_metric_value != 0) {
            has_host_custom_metrics = true;
            break;
          }
        }
        should_send_locality_stats = rq_success != 0 || rq_error != 0 || rq_active != 0 ||
                                     rq_issued != 0 || has_host_custom_metrics;
      } else {
        // If rq_active is non-zero, we should send the locality stats even if
        // rq_issued is zero (no new requests have been issued in this poll
        // window). This is needed to report long-lived connections/requests (e.g., when
        // web-sockets are used).
        should_send_locality_stats = (rq_issued != 0) || (rq_active != 0);
      }

      if (should_send_locality_stats) {
        locality_stats->set_total_successful_requests(rq_success);
        locality_stats->set_total_error_requests(rq_error);
        locality_stats->set_total_requests_in_progress(rq_active);
        locality_stats->set_total_issued_requests(rq_issued);
        for (const auto& metric : aggregated_host_custom_metrics) {
          auto* load_metric_stats = locality_stats->add_load_metric_stats();
          load_metric_stats->set_metric_name(metric.first);
          load_metric_stats->set_num_requests_finished_with_metric(
              metric.second.num_requests_with_metric);
          load_metric_stats->set_total_metric_value(metric.second.total_metric_value);
        }
      } else {
        // If no upstream locality stats need to be sent, this trims the empty message that was
        // added near the beginning of the host set loop.
        cluster_stats.mutable_upstream_locality_stats()->RemoveLast();
      }
    }
  }
  return requests_in_progress;
}

void LoadStatsReporterImpl::handleFailure() {
  stats_.errors_.inc();
  setRetryTimer();
}

void LoadStatsReporterImpl::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporterImpl::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporterImpl::onReceiveMessage(
    Grpc::ResponsePtr<envoy::service::load_stats::v3::LoadStatsResponse>&& message) {
  ENVOY_LOG(debug, "New load report epoch: {}", message->DebugString());
  message_ = std::move(message);
  startLoadReportPeriod();
  stats_.requests_.inc();
}

bool LoadStatsReporterImpl::trackCluster(absl::string_view cluster_name,
                                         std::chrono::steady_clock::duration now) {
  const auto [it, inserted] = clusters_.try_emplace(cluster_name, now);
  it->second.generation = generation_;
  return inserted;
}

void LoadStatsReporterImpl::startLoadReportPeriod() {
  // Once a cluster is tracked, we don't want to reset its stats between reports
  // to avoid racing between request/response.
  const auto now = time_source_.monotonicTime().time_since_epoch();
  ++generation_;
  if (message_->send_all_clusters()) {
    cm_.forEachActiveCluster([this, now](const Cluster& cluster) {
      if (trackCluster(cluster.info()->name(), now)) {
        latchClusterLoadStats(cluster);
      }
    });
  } else {
    for (const std::string& cluster_name : message_->clusters()) {
      if (trackCluster(cluster_name, now)) {
        // The cluster may not exist (yet). Its stats are then not latched when it appears, so that
        // all of its load is reported, consistent with the measurement interval starting now.
        if (OptRef<const Cluster> cluster = cm_.getActiveCluster(cluster_name);
            cluster.has_value()) {
          latchClusterLoadStats(*cluster);
        }
      }
    }
  }
  // Stop tracking clusters that are no longer selected by the LRS server. With send_all_clusters
  // this also drops clusters that no longer exist.
  absl::erase_if(clusters_,
                 [this](const auto& entry) { return entry.second.generation != generation_; });
  response_timer_->enableTimer(std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(message_->load_reporting_interval())));
}

void LoadStatsReporterImpl::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporterImpl::onRemoteClose(Grpc::Status::GrpcStatus status,
                                          const std::string& message) {
  response_timer_->disableTimer();
  stream_ = nullptr;
  if (status != Grpc::Status::WellKnownGrpcStatus::Ok) {
    ENVOY_LOG(warn, "{} gRPC config stream closed: {}, {}", service_method_.name(), status,
              message);
    handleFailure();
  } else {
    ENVOY_LOG(debug, "{} gRPC config stream closed gracefully, {}", service_method_.name(),
              message);
    setRetryTimer();
  }
}
} // namespace Upstream
} // namespace Envoy
