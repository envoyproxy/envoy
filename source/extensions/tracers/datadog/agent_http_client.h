#pragma once

#include <string>

#include "envoy/http/async_client.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/cluster_update_tracker.h"

#include "absl/container/flat_hash_map.h"
#include "datadog/http_client.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

struct TracerStats;

/**
 * \c datadog::tracing::HTTPClient implementation that uses Envoy's
 * \c Http::AsyncClient. The class is called \c AgentHTTPClient because it is
 * not a general-purpose HTTP client. Instead, it sends requests to a specified
 * cluster only. The idea is that the cluster is configured to point to a
 * Datadog Agent instance.
 */
class AgentHTTPClient : public datadog::tracing::HTTPClient,
                        public Http::AsyncClient::Callbacks,
                        private Logger::Loggable<Logger::Id::tracing> {
public:
  struct Handlers {
    ResponseHandler on_response;
    ErrorHandler on_error;
  };

public:
  /**
   * Create an  \c AgentHTTPClient that uses the specified \p cluster_manager
   * to make requests to the specified \p cluster, where requests include the
   * specified \p reference_host as the "Host" header. Use the specified
   * \p stats to keep track of usage statistics.
   * @param cluster_manager cluster manager from which the thread local cluster
   * is retrieved in order to make HTTP requests
   * @param cluster the name of the cluster to which HTTP requests are made
   * @param reference_host the value to use for the "Host" HTTP request header
   * @param stats a collection of counters used to keep track of events, such as
   * when a request fails
   * @param time_source clocks used for calculating request timeouts
   */
  AgentHTTPClient(Upstream::ClusterManager& cluster_manager, const std::string& cluster,
                  const std::string& reference_host, TracerStats& stats, TimeSource& time_source);
  ~AgentHTTPClient() override;

  // datadog::tracing::HTTPClient

  /**
   * Send an HTTP POST request to the cluster, where the requested resource is
   * \p url.path. Invoke \p set_headers immediately to obtain the HTTP request
   * headers. Send \p body as the request body. Return a
   * \c datadog::tracing::Error if one occurs, otherwise return
   * \c datadog::tracing::nullopt. When a complete response is received,
   * invoke \p on_response with the response status, response headers, and
   * response body. If an error occurs before a complete response is received,
   * invoke \p on_error with a \c datadog::tracing::Error.
   * @param url URL from which the request path is taken
   * @param set_headers callback invoked immediately to obtain request headers
   * @param body data to send as POST request body
   * @param on_response callback to invoke when a complete response is received
   * @param on_error callback to invoke when an error occurs before a complete
   * response is received
   * @param deadline time after which a response is not expected
   * @return \c datadog::tracing::Error if an error occurs, or
   * \c datadog::tracing::nullopt otherwise
   */
  datadog::tracing::Expected<void> post(const URL& url, HeadersSetter set_headers, std::string body,
                                        ResponseHandler on_response, ErrorHandler on_error,
                                        std::chrono::steady_clock::time_point deadline) override;

  /**
   * \c drain has no effect. It's a part of the \c datadog::tracing::HTTPClient
   * that we do not need.
   */
  void drain(std::chrono::steady_clock::time_point) override;

  /**
   * Return a JSON representation of this object's configuration. This function
   * is used in the startup banner logged by \c dd-trace-cpp.
   */
  nlohmann::json config_json() const override;

  // Http::AsyncClient::Callbacks

  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override;

private:
  absl::flat_hash_map<Http::AsyncClient::Request*, Handlers> handlers_;
  Upstream::ClusterUpdateTracker collector_cluster_;
  const std::string cluster_;
  const std::string reference_host_;
  TracerStats& stats_;
  TimeSource& time_source_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
