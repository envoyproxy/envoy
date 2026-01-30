#include "source/common/upstream/load_stats_reporter.h"

#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

namespace {

envoy::service::load_stats::v3::LoadStatsRequest
MakeRequestTemplate(const LocalInfo::LocalInfo& local_info) {
  envoy::service::load_stats::v3::LoadStatsRequest request;
  request.mutable_node()->MergeFrom(local_info.node());
  request.mutable_node()->add_client_features("envoy.lrs.supports_send_all_clusters");
  return request;
}

} // namespace

LoadStatsReporter::LoadStatsReporter(const LocalInfo::LocalInfo& local_info,
                                     ClusterManager& cluster_manager, Stats::Scope& scope,
                                     Grpc::RawAsyncClientSharedPtr&& async_client,
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

LoadStatsReporter::~LoadStatsReporter() {
  // Disable the timer.
  ENVOY_LOG_MISC(info, "Destroying LoadStatsReporter");
  retry_timer_->disableTimer();
  response_timer_->disableTimer();
  if (stream_ != nullptr) {
    stream_->resetStream();
    stream_ = nullptr;
  }
}

void LoadStatsReporter::setRetryTimer() {
  ENVOY_LOG(info, "Load reporter stats stream/connection will retry in {} ms.", RETRY_DELAY_MS);
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void LoadStatsReporter::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  sendLoadStatsRequest();
}

void LoadStatsReporter::sendLoadStatsRequest() {
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
  for (const auto& cluster_name_and_timestamp : clusters_) {
    const std::string& cluster_name = cluster_name_and_timestamp.first;
    OptRef<const Upstream::Cluster> active_cluster = cm_.getActiveCluster(cluster_name);
    if (!active_cluster.has_value()) {
      ENVOY_LOG(debug, "Cluster {} does not exist", cluster_name);
      continue;
    }
    const Upstream::Cluster& cluster = active_cluster.value();
    auto* cluster_stats = request->add_cluster_stats();
    cluster_stats->set_cluster_name(cluster_name);
    if (const auto& name = cluster.info()->edsServiceName(); !name.empty()) {
      cluster_stats->set_cluster_service_name(name);
    }
    for (const HostSetPtr& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      ENVOY_LOG(trace, "Load report locality count {}", host_set->hostsPerLocality().get().size());
      for (const HostVector& hosts : host_set->hostsPerLocality().get()) {
        ASSERT(!hosts.empty());
        uint64_t rq_success = 0;
        uint64_t rq_error = 0;
        uint64_t rq_active = 0;
        uint64_t rq_issued = 0;
        LoadMetricStats::StatMap load_metrics;

        envoy::config::endpoint::v3::UpstreamLocalityStats locality_stats;
        locality_stats.mutable_locality()->MergeFrom(hosts[0]->locality());
        locality_stats.set_priority(host_set->priority());

        for (const HostSharedPtr& host : hosts) {
          uint64_t host_rq_success = host->stats().rq_success_.latch();
          uint64_t host_rq_error = host->stats().rq_error_.latch();
          uint64_t host_rq_active = host->stats().rq_active_.value();
          uint64_t host_rq_issued = host->stats().rq_total_.latch();

          // Check if the host has any load stats updates. If the host has no load stats updates, we
          // skip it.
          bool endpoint_has_updates =
              (host_rq_success + host_rq_error + host_rq_active + host_rq_issued) != 0;

          if (endpoint_has_updates) {
            rq_success += host_rq_success;
            rq_error += host_rq_error;
            rq_active += host_rq_active;
            rq_issued += host_rq_issued;

            envoy::config::endpoint::v3::UpstreamEndpointStats* upstream_endpoint_stats = nullptr;
            // Set the upstream endpoint stats if we are reporting endpoint granularity.
            if (message_ && message_->report_endpoint_granularity()) {
              upstream_endpoint_stats = locality_stats.add_upstream_endpoint_stats();
              Network::Utility::addressToProtobufAddress(
                  *host->address(), *upstream_endpoint_stats->mutable_address());
              upstream_endpoint_stats->set_total_successful_requests(host_rq_success);
              upstream_endpoint_stats->set_total_error_requests(host_rq_error);
              upstream_endpoint_stats->set_total_requests_in_progress(host_rq_active);
              upstream_endpoint_stats->set_total_issued_requests(host_rq_issued);
            }

            const std::unique_ptr<LoadMetricStats::StatMap> latched_stats =
                host->loadMetricStats().latch();
            if (latched_stats != nullptr) {
              for (const auto& metric : *latched_stats) {
                const auto& metric_name = metric.first;
                const auto& metric_value = metric.second;

                // Add the metric to the load metrics map.
                LoadMetricStats::Stat& stat = load_metrics[metric_name];
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

        bool should_send_locality_stats = rq_issued != 0;
        if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features."
                                           "report_load_when_rq_active_is_non_zero")) {
          // If rq_active is non-zero, we should send the locality stats even if
          // rq_issued is zero (no new requests have been issued in this poll
          // window). This is needed to report long-lived connections/requests (e.g., when
          // web-sockets are used).
          should_send_locality_stats = should_send_locality_stats || (rq_active != 0);
        }

        if (should_send_locality_stats) {
          locality_stats.set_total_successful_requests(rq_success);
          locality_stats.set_total_error_requests(rq_error);
          locality_stats.set_total_requests_in_progress(rq_active);
          locality_stats.set_total_issued_requests(rq_issued);
          for (const auto& metric : load_metrics) {
            auto* load_metric_stats = locality_stats.add_load_metric_stats();
            load_metric_stats->set_metric_name(metric.first);
            load_metric_stats->set_num_requests_finished_with_metric(
                metric.second.num_requests_with_metric);
            load_metric_stats->set_total_metric_value(metric.second.total_metric_value);
          }
          cluster_stats->add_upstream_locality_stats()->MergeFrom(locality_stats);
        }
      }
    }
    cluster_stats->set_total_dropped_requests(
        cluster.info()->loadReportStats().upstream_rq_dropped_.latch());
    const uint64_t drop_overload_count =
        cluster.info()->loadReportStats().upstream_rq_drop_overload_.latch();
    if (drop_overload_count > 0) {
      auto* dropped_request = cluster_stats->add_dropped_requests();
      dropped_request->set_category(cluster.dropCategory());
      dropped_request->set_dropped_count(drop_overload_count);
    }

    const auto now = time_source_.monotonicTime().time_since_epoch();
    const auto measured_interval = now - cluster_name_and_timestamp.second;
    cluster_stats->mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(
            std::chrono::duration_cast<std::chrono::microseconds>(measured_interval).count()));
    clusters_[cluster_name] = now;
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

void LoadStatsReporter::handleFailure() {
  stats_.errors_.inc();
  setRetryTimer();
}

void LoadStatsReporter::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporter::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporter::onReceiveMessage(
    std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse>&& message) {
  ENVOY_LOG(debug, "New load report epoch: {}", message->DebugString());
  message_ = std::move(message);
  startLoadReportPeriod();
  stats_.requests_.inc();
}

void LoadStatsReporter::startLoadReportPeriod() {
  // Once a cluster is tracked, we don't want to reset its stats between reports
  // to avoid racing between request/response.
  // TODO(htuch): They key here could be absl::string_view, but this causes
  // problems due to referencing of temporaries in the below loop with Google's
  // internal string type. Consider this optimization when the string types
  // converge.
  const ClusterManager::ClusterInfoMaps all_clusters = cm_.clusters();
  absl::node_hash_map<std::string, std::chrono::steady_clock::duration> existing_clusters;
  if (message_->send_all_clusters()) {
    for (const auto& p : all_clusters.active_clusters_) {
      const std::string& cluster_name = p.first;
      auto it = clusters_.find(cluster_name);
      if (it != clusters_.end()) {
        existing_clusters.emplace(cluster_name, it->second);
      }
    }
  } else {
    for (const std::string& cluster_name : message_->clusters()) {
      auto it = clusters_.find(cluster_name);
      if (it != clusters_.end()) {
        existing_clusters.emplace(cluster_name, it->second);
      }
    }
  }
  clusters_.clear();
  // Reset stats for all hosts in clusters we are tracking.
  auto handle_cluster_func = [this, &existing_clusters,
                              &all_clusters](const std::string& cluster_name) {
    auto existing_cluster_it = existing_clusters.find(cluster_name);
    clusters_.emplace(cluster_name, existing_cluster_it != existing_clusters.end()
                                        ? existing_cluster_it->second
                                        : time_source_.monotonicTime().time_since_epoch());
    auto it = all_clusters.active_clusters_.find(cluster_name);
    if (it == all_clusters.active_clusters_.end()) {
      return;
    }
    // Don't reset stats for existing tracked clusters.
    if (existing_cluster_it != existing_clusters.end()) {
      return;
    }
    auto& cluster = it->second.get();
    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      for (const auto& host : host_set->hosts()) {
        host->stats().rq_success_.latch();
        host->stats().rq_error_.latch();
        host->stats().rq_total_.latch();
      }
    }
    cluster.info()->loadReportStats().upstream_rq_dropped_.latch();
    cluster.info()->loadReportStats().upstream_rq_drop_overload_.latch();
  };
  if (message_->send_all_clusters()) {
    for (const auto& p : all_clusters.active_clusters_) {
      const std::string& cluster_name = p.first;
      handle_cluster_func(cluster_name);
    }
  } else {
    for (const std::string& cluster_name : message_->clusters()) {
      handle_cluster_func(cluster_name);
    }
  }
  response_timer_->enableTimer(std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(message_->load_reporting_interval())));
}

void LoadStatsReporter::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void LoadStatsReporter::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
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
