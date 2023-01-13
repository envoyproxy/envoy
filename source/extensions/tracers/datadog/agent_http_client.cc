#include "source/extensions/tracers/datadog/agent_http_client.h"

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/error.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <datadog/json.hpp>
#include <memory>
#include <string>

#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/string_util.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

AgentHTTPClient::AgentHTTPClient(Upstream::ClusterManager& cluster_manager,
                                 const std::string& cluster, const std::string& reference_host,
                                 TracerStats& stats)
    : collector_cluster_(cluster_manager, cluster), cluster_(cluster),
      reference_host_(reference_host), stats_(&stats) {}

AgentHTTPClient::~AgentHTTPClient() {
  for (const auto& [request_ptr, _] : handlers_) {
    assert(request_ptr);
    request_ptr->cancel();
  }
}

// dd::HTTPClient

dd::Expected<void> AgentHTTPClient::post(const URL& url, HeadersSetter set_headers,
                                         std::string body, ResponseHandler on_response,
                                         ErrorHandler on_error) {
  ENVOY_LOG(debug, "flushing traces");

  auto message = std::make_unique<Http::RequestMessageImpl>();
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferencePath(url.path);
  message->headers().setReferenceHost(reference_host_);

  RequestHeaderWriter writer{message->headers()};
  set_headers(writer);

  message->body().add(body);
  ENVOY_LOG(debug, "submitting trace(s) to {} with payload size {}", url.path, body.size());

  if (!collector_cluster_.threadLocalCluster().has_value()) {
    ENVOY_LOG(debug, "collector cluster '{}' does not exist", cluster_);
    stats_->reports_skipped_no_cluster_.inc();
    return dd::nullopt;
  }

  Http::AsyncClient::Request* request =
      collector_cluster_.threadLocalCluster()->get().httpAsyncClient().send(
          std::move(message), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(1000)));
  if (!request) {
    stats_->reports_failed_.inc();
    return dd::Error{dd::Error::ENVOY_HTTP_CLIENT_FAILURE, "Failed to create request."};
  }

  handlers_.emplace(request, Handlers{std::move(on_response), std::move(on_error)});
  return dd::nullopt;
}

void AgentHTTPClient::drain(std::chrono::steady_clock::time_point) {}

nlohmann::json AgentHTTPClient::config_json() const {
  return nlohmann::json::object({
      {"type", "Envoy::Extensions::Tracers::Datadog::AgentHTTPClient"},
  });
}

// Http::AsyncClient::Callbacks

void AgentHTTPClient::onSuccess(const Http::AsyncClient::Request& request,
                                Http::ResponseMessagePtr&& response) {
  const std::uint64_t status = Http::Utility::getResponseStatus(response->headers());
  if (status != std::uint64_t(Http::Code::OK)) {
    stats_->reports_dropped_.inc();
  } else {
    ENVOY_LOG(debug, "traces successfully submitted to datadog agent");
    stats_->reports_sent_.inc();
  }

  auto found = handlers_.find(const_cast<Http::AsyncClient::Request*>(&request));
  if (found == handlers_.end()) {
    ENVOY_LOG(debug, "request at address 0x{} is not in the map of {} Handlers objects",
              hex(reinterpret_cast<std::uintptr_t>(&request)), handlers_.size());
    return;
  }

  auto& handlers = found->second;
  ResponseHeaderReader reader{response->headers()};
  handlers.on_response(status, reader, response->bodyAsString());

  handlers_.erase(found);
}

void AgentHTTPClient::onFailure(const Http::AsyncClient::Request& request,
                                Http::AsyncClient::FailureReason reason) {
  auto found = handlers_.find(const_cast<Http::AsyncClient::Request*>(&request));
  // If the request failed to start, then `post` never even registered the request.
  if (found == handlers_.end()) {
    return;
  }

  stats_->reports_failed_.inc();

  auto& handlers = found->second;
  std::string message;
  message += "Failed to send request to Datadog Agent: ";
  switch (reason) {
  case Http::AsyncClient::FailureReason::Reset:
    message += "The stream has been reset.";
    break;
  default:
    message += "Unknown error.";
  }
  handlers.on_error(dd::Error{dd::Error::ENVOY_HTTP_CLIENT_FAILURE, std::move(message)});

  handlers_.erase(found);
}

void AgentHTTPClient::onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) {
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
