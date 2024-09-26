#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

void ExtProcHttpClient::setCallbacks(RequestCallbacks* callbacks) {
  callbacks_ = callbacks;
  ENVOY_LOG(debug, "ext_proc http client set callbacks_ == nullptr? {}", (callbacks_ == nullptr));
}

void ExtProcHttpClient::sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& req, bool,
                                    const uint64_t stream_id, RequestCallbacks* callbacks,
                                    StreamBase*) {
  setCallbacks(callbacks);
  // Cancel any active requests.
  cancel();

  // Transcode req message into JSON string.
  auto req_in_json = MessageUtil::getJsonStringFromMessage(req);
  if (req_in_json.ok()) {
    const auto http_uri = config_.http_service().http_service().http_uri();
    const std::string cluster = http_uri.cluster();
    const std::string uri = http_uri.uri();
    absl::string_view host, path;
    Envoy::Http::Utility::extractHostPathFromUri(uri, host, path);
    ENVOY_LOG(debug, " Ext_Proc HTTP client send request to cluster {}, uri {}, host {}, path {}",
              cluster, uri, host, path);

    const auto thread_local_cluster = context().clusterManager().getThreadLocalCluster(cluster);

    // Construct a HTTP POST message and sends to the ext_proc server cluster.
    Http::RequestHeaderMapPtr headers =
        Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
            {{Envoy::Http::Headers::get().Method, "POST"},
             {Envoy::Http::Headers::get().Scheme, "http"},
             {Envoy::Http::Headers::get().Path, std::string(path)},
             {Envoy::Http::Headers::get().ContentType, "application/json"},
             {Envoy::Http::Headers::get().RequestId, std::to_string(stream_id)},
             {Envoy::Http::Headers::get().Host, std::string(host)}});
    Http::RequestMessagePtr message =
        std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
    message->body().add(req_in_json.value());

    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(http_uri.timeout())))
                       .setSampled(absl::nullopt)
                       .setSendXff(false);

    active_request_ =
        thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);
  }
}

void ExtProcHttpClient::onSuccess(const Http::AsyncClient::Request&,
                                  Http::ResponseMessagePtr&& response) {
  auto status = Envoy::Http::Utility::getResponseStatusOrNullopt(response->headers());
  active_request_ = nullptr;
  if (status.has_value()) {
    uint64_t status_code = status.value();
    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      std::string msg_body = response->body().toString();
      ENVOY_LOG(debug, "Response status is OK, message body length {}", msg_body.size());
      envoy::service::ext_proc::v3::ProcessingResponse response_msg;
      if (!msg_body.empty()) {
        bool has_unknown_field;
        auto status = MessageUtil::loadFromJsonNoThrow(msg_body, response_msg, has_unknown_field);
        if (!status.ok()) {
          ENVOY_LOG(
              error,
              "The HTTP response body can not be decoded into a ProcessResponse proto message");
          onError();
          return;
        }
      }
      if (callbacks_) {
        callbacks_->onComplete(response_msg);
      }
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
  active_request_ = nullptr;
  onError();
}

void ExtProcHttpClient::onError() {
  // Cancel if the request is active.
  cancel();
  ENVOY_LOG(error, "ext_proc HTTP client error condition happens.");
  if (callbacks_) {
    callbacks_->onError();
  }
}

void ExtProcHttpClient::cancel() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
