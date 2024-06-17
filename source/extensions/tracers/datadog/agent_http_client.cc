#include "source/extensions/tracers/datadog/agent_http_client.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/tracers/datadog/dict_util.h"
#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "absl/strings/str_format.h"
#include "datadog/dict_reader.h"
#include "datadog/dict_writer.h"
#include "datadog/error.h"
#include "datadog/json.hpp"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

AgentHTTPClient::AgentHTTPClient(Upstream::ClusterManager& cluster_manager,
                                 const std::string& cluster, const std::string& reference_host,
                                 TracerStats& stats, TimeSource& time_source)
    : collector_cluster_(cluster_manager, cluster), cluster_(cluster),
      reference_host_(reference_host), stats_(stats), time_source_(time_source) {}

AgentHTTPClient::~AgentHTTPClient() {
  for (const auto& [request_ptr, _] : handlers_) {
    RELEASE_ASSERT(request_ptr,
                   "null Http::AsyncClient::Request* in handler map of Datadog::AgentHTTPClient");
    request_ptr->cancel();
  }
}

// datadog::tracing::HTTPClient

datadog::tracing::Expected<void>
AgentHTTPClient::post(const URL& url, HeadersSetter set_headers, std::string body,
                      ResponseHandler on_response, ErrorHandler on_error,
                      std::chrono::steady_clock::time_point deadline) {
  auto message = std::make_unique<Http::RequestMessageImpl>();
  Http::RequestHeaderMap& headers = message->headers();
  headers.setReferenceMethod(Http::Headers::get().MethodValues.Post);
  headers.setReferencePath(url.path);
  headers.setReferenceHost(reference_host_);

  RequestHeaderWriter writer{message->headers()};
  set_headers(writer);

  message->body().add(body);

  const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - time_source_.monotonicTime());
  if (timeout.count() <= 0) {
    std::string message = "request deadline expired already";
    if (timeout.count() < 0) {
      message += ' ';
      message += std::to_string(-timeout.count());
      message += " milliseconds ago";
    }
    stats_.reports_dropped_.inc();
    return datadog::tracing::Error{datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE,
                                   std::move(message)};
  }

  ENVOY_LOG(debug, "submitting data to {} with payload size {} and {} ms timeout", url.path,
            body.size(), timeout.count());

  if (!collector_cluster_.threadLocalCluster().has_value()) {
    ENVOY_LOG(debug, "collector cluster '{}' does not exist", cluster_);
    stats_.reports_skipped_no_cluster_.inc();
    return datadog::tracing::nullopt;
  }

  Http::AsyncClient::Request* request =
      collector_cluster_.threadLocalCluster()->get().httpAsyncClient().send(
          std::move(message), *this, Http::AsyncClient::RequestOptions().setTimeout(timeout));
  if (!request) {
    stats_.reports_failed_.inc();
    return datadog::tracing::Error{datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE,
                                   "Failed to create request."};
  }

  handlers_.emplace(request, Handlers{std::move(on_response), std::move(on_error)});
  return datadog::tracing::nullopt;
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
    stats_.reports_dropped_.inc();
  } else {
    ENVOY_LOG(debug, "traces successfully submitted to datadog agent");
    stats_.reports_sent_.inc();
  }

  auto found = handlers_.find(const_cast<Http::AsyncClient::Request*>(&request));
  if (found == handlers_.end()) {
    ENVOY_LOG(debug, "request at address 0x{} is not in the map of {} Handlers objects",
              absl::StrFormat("%p", &request), handlers_.size());
    return;
  }

  Handlers& handlers = found->second;
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

  stats_.reports_failed_.inc();

  Handlers& handlers = found->second;
  std::string message = "Failed to send request to Datadog Agent: ";
  switch (reason) {
  case Http::AsyncClient::FailureReason::Reset:
    message += "The stream has been reset.";
    break;
  case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
    message += "The stream exceeds the response buffer limit.";
    break;
  default:
    message += "Unknown error.";
  }
  handlers.on_error(datadog::tracing::Error{datadog::tracing::Error::ENVOY_HTTP_CLIENT_FAILURE,
                                            std::move(message)});

  handlers_.erase(found);
}

void AgentHTTPClient::onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) {
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
