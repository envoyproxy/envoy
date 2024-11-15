#include "source/extensions/config_subscription/rest/rest_api_fetcher.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "source/common/common/enum_to_int.h"
#include "source/common/config/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Http {

RestApiFetcher::RestApiFetcher(Upstream::ClusterManager& cm, const std::string& remote_cluster_name,
                               Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
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

void RestApiFetcher::onSuccess(const Http::AsyncClient::Request& request,
                               Http::ResponseMessagePtr&& response) {
  uint64_t response_code = Http::Utility::getResponseStatus(response->headers());
  if (response_code == enumToInt(Http::Code::NotModified)) {
    requestComplete();
    return;
  } else if (response_code != enumToInt(Http::Code::OK)) {
    onFailure(request, Http::AsyncClient::FailureReason::Reset);
    return;
  }

  TRY_ASSERT_MAIN_THREAD { parseResponse(*response); }
  END_TRY
  catch (EnvoyException& e) {
    onFetchFailure(Config::ConfigUpdateFailureReason::UpdateRejected, &e);
  }

  requestComplete();
}

void RestApiFetcher::onFailure(const Http::AsyncClient::Request&,
                               Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset ||
         reason == Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
  onFetchFailure(Config::ConfigUpdateFailureReason::ConnectionFailure, nullptr);
  requestComplete();
}

void RestApiFetcher::refresh() {
  RequestMessagePtr message(new RequestMessageImpl());
  createRequest(*message);
  message->headers().setHost(remote_cluster_name_);
  const auto thread_local_cluster = cm_.getThreadLocalCluster(remote_cluster_name_);
  if (thread_local_cluster != nullptr) {
    active_request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this, AsyncClient::RequestOptions().setTimeout(request_timeout_));
  } else {
    onFetchFailure(Config::ConfigUpdateFailureReason::ConnectionFailure, nullptr);
    requestComplete();
  }
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
