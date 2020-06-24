#include "envoy/event/dispatcher.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"
#include "common/grpc/typed_async_client.h"

namespace Envoy {
namespace Upstream {

/**
 * All load reporter stats. @see stats_macros.h
 */
#define ALL_LOAD_REPORTER_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)

/**
 * Struct definition for all load reporter stats. @see stats_macros.h
 */
struct LoadReporterStats {
  ALL_LOAD_REPORTER_STATS(GENERATE_COUNTER_STRUCT)
};

class LoadStatsReporter
    : Grpc::AsyncStreamCallbacks<envoy::service::load_stats::v3::LoadStatsResponse>,
      Logger::Loggable<Logger::Id::upstream> {
public:
  LoadStatsReporter(const LocalInfo::LocalInfo& local_info, ClusterManager& cluster_manager,
                    Stats::Scope& scope, Grpc::RawAsyncClientPtr async_client,
                    envoy::config::core::v3::ApiVersion transport_api_version,
                    Event::Dispatcher& dispatcher);

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;

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
  const envoy::config::core::v3::ApiVersion transport_api_version_;
  Grpc::AsyncStream<envoy::service::load_stats::v3::LoadStatsRequest> stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  envoy::service::load_stats::v3::LoadStatsRequest request_;
  std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse> message_;
  // Map from cluster name to start of measurement interval.
  std::unordered_map<std::string, std::chrono::steady_clock::duration> clusters_;
  TimeSource& time_source_;
};

using LoadStatsReporterPtr = std::unique_ptr<LoadStatsReporter>;

} // namespace Upstream
} // namespace Envoy
