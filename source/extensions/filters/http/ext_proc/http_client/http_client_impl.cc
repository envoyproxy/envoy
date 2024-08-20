#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

void ExtProcHttpClient::sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& req, bool/* end_stream*/) {
  std::string cluster = config_.http_service().http_service().http_uri().cluster();
  std::string url = config_.http_service().http_service().http_uri().uri();
  absl::string_view host;
  absl::string_view path;
  Envoy::Http::Utility::extractHostPathFromUri(url, host, path);
  ENVOY_LOG(debug, " Ext_Proc HTTP client send request to cluster {}, url {}, host {}, path {}",
            cluster, url, host, path);

  const auto thread_local_cluster = context().clusterManager().getThreadLocalCluster(cluster);
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, "POST"},
           {Envoy::Http::Headers::get().Scheme, "http"},
           {Envoy::Http::Headers::get().Path, std::string(path)},
           {Envoy::Http::Headers::get().Host, std::string(host)}});
  Http::RequestMessagePtr message =
      std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
  message->body().add(MessageUtil::getJsonStringFromMessageOrError(req, true, true));
  auto options = Http::AsyncClient::RequestOptions()
                 .setTimeout(std::chrono::milliseconds(200))
                 .setSampled(absl::nullopt)
                 .setSendXff(false);

  (void)thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);
}

void ExtProcHttpClient::onSuccess(const Http::AsyncClient::Request&,
                                  Http::ResponseMessagePtr&& response) {
  auto status = Envoy::Http::Utility::getResponseStatusOrNullopt(response->headers());
  if (status.has_value()) {
    uint64_t status_code = status.value();
    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      ENVOY_LOG(error, "Response status is OK");
      ASSERT(callbacks_ != nullptr);
      callbacks_->onComplete();
      callbacks_ = nullptr;
    } else {
      ENVOY_LOG(error, "Response status is not OK, status: {}", status_code);
      onError();
    }
  } else {
    // This occurs if the response headers are invalid.
    ENVOY_LOG(error, "Failed to get the response because response headers are not valid.");
    onError();
  }
}

void ExtProcHttpClient::onFailure(const Http::AsyncClient::Request&,
                                  Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset ||
         reason == Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
  ENVOY_LOG(error, "Request failed: stream has been reset");
  onError();
}

void ExtProcHttpClient::onError() {
  // Cancel if the request is active.
  cancel();
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
