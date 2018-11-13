#include "common/http/rest_api_fetcher.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "common/common/enum_to_int.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

RestApiFetcher::RestApiFetcher(Upstream::ClusterManager& cm,
                               const envoy::api::v2::core::ApiConfigSource& api_config_source,
                               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random)
    : RestApiFetcher(cm, api_config_source.cluster_names()[0], dispatcher, random,
                     Config::Utility::apiConfigSourceRefreshDelay(api_config_source),
                     Config::Utility::apiConfigSourceRequestTimeout(api_config_source)) {
  UNREFERENCED_PARAMETER(api_config_source);
  // The ApiType must be REST_LEGACY for xDS implementations that call this constructor.
  ASSERT(api_config_source.api_type() == envoy::api::v2::core::ApiConfigSource::REST_LEGACY);
  // TODO(htuch): Add support for multiple clusters, #1170.
  ASSERT(api_config_source.cluster_names().size() == 1);
  ASSERT(api_config_source.has_refresh_delay());
}

RestApiFetcher::RestApiFetcher(Upstream::ClusterManager& cm, const std::string& remote_cluster_name,
                               Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                               std::chrono::milliseconds refresh_interval,
                               std::chrono::milliseconds request_timeout)
    : remote_cluster_name_(remote_cluster_name), cm_(cm), random_(random),
      refresh_interval_(refresh_interval), request_timeout_(request_timeout),
      refresh_timer_(dispatcher.createTimer([this]() -> void { refresh(); })) {}

RestApiFetcher::~RestApiFetcher() {
  if (active_request_) {
    active_request_->cancel();
  }
}

void RestApiFetcher::initialize() { refresh(); }

void RestApiFetcher::onSuccess(Http::MessagePtr&& response) {
  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  if (response_code != enumToInt(Http::Code::OK)) {
    onFailure(Http::AsyncClient::FailureReason::Reset);
    return;
  }

  try {
    parseResponse(*response);
  } catch (EnvoyException& e) {
    onFetchFailure(&e);
  }

  requestComplete();
}

void RestApiFetcher::onFailure(Http::AsyncClient::FailureReason) {
  onFetchFailure(nullptr);
  requestComplete();
}

void RestApiFetcher::refresh() {
  MessagePtr message(new RequestMessageImpl());
  createRequest(*message);
  message->headers().insertHost().value(remote_cluster_name_);
  active_request_ = cm_.httpAsyncClientForCluster(remote_cluster_name_)
                        .send(std::move(message), *this,
                              AsyncClient::RequestOptions().setTimeout(request_timeout_));
}

void RestApiFetcher::requestComplete() {
  onFetchComplete();
  active_request_ = nullptr;

  // Add refresh jitter based on the configured interval.
  std::chrono::milliseconds final_delay =
      refresh_interval_ + std::chrono::milliseconds(random_.random() % refresh_interval_.count());

  refresh_timer_->enableTimer(final_delay);
}

} // namespace Http
} // namespace Envoy
