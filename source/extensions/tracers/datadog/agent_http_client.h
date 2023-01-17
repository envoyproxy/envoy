#pragma once

#include <datadog/http_client.h>

#include <string>

#include "envoy/http/async_client.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_update_tracker.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

struct TracerStats;

class AgentHTTPClient : public datadog::tracing::HTTPClient,
                        public Http::AsyncClient::Callbacks,
                        private Logger::Loggable<Logger::Id::tracing> {
public:
  struct Handlers {
    ResponseHandler on_response;
    ErrorHandler on_error;
  };

private:
  absl::flat_hash_map<Http::AsyncClient::Request*, Handlers> handlers_;
  Upstream::ClusterUpdateTracker collector_cluster_;
  std::string cluster_;
  std::string reference_host_;
  TracerStats* stats_;

public:
  // Create an `AgentHTTPClient` that uses the specified `ClusterManager` to
  // make requests to the specified `cluster`, where requests include the
  // specified `reference_host` as the "Host" header. Use the specified
  // `TracerStats` to keep track of usage statistics.
  AgentHTTPClient(Upstream::ClusterManager&, const std::string& cluster,
                  const std::string& reference_host, TracerStats&);

  ~AgentHTTPClient() override;

  // datadog::tracing::HTTPClient

  datadog::tracing::Expected<void> post(const URL& url, HeadersSetter set_headers, std::string body,
                                        ResponseHandler on_response,
                                        ErrorHandler on_error) override;

  // `drain` has no effect.
  void drain(std::chrono::steady_clock::time_point) override;

  nlohmann::json config_json() const override;

  // Http::AsyncClient::Callbacks

  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
