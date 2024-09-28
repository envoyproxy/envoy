#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

namespace {
Http::RequestMessagePtr buildHttpRequest(absl::string_view uri, const uint64_t stream_id,
                                         absl::string_view req_in_json) {
  absl::string_view host, path;
  Envoy::Http::Utility::extractHostPathFromUri(uri, host, path);
  ENVOY_LOG_MISC(debug, " Ext_Proc HTTP client send request to uri {}, host {}, path {}", uri, host,
                 path);

  // Construct a HTTP POST message.
  const Envoy::Http::HeaderValues& header_values = Envoy::Http::Headers::get();
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{header_values.Method, "POST"},
           {header_values.Scheme, "http"},
           {header_values.Path, std::string(path)},
           {header_values.ContentType, "application/json"},
           {header_values.RequestId, std::to_string(stream_id)},
           {header_values.Host, std::string(host)}});
  Http::RequestMessagePtr message =
      std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
  message->body().add(req_in_json);
  return message;
}

} // namespace

void ExtProcHttpClient::sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& req, bool,
                                    const uint64_t stream_id, RequestCallbacks* callbacks,
                                    StreamBase*) {
  // Cancel any active requests.
  cancel();
  callbacks_ = callbacks;

  // Transcode req message into JSON string.
  auto req_in_json = MessageUtil::getJsonStringFromMessage(req);
  if (req_in_json.ok()) {
    const auto http_uri = config_.http_service().http_service().http_uri();
    Http::RequestMessagePtr message =
        buildHttpRequest(http_uri.uri(), stream_id, req_in_json.value());
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(http_uri.timeout())))
                       .setSendInternal(false)
                       .setSendXff(false);
    const std::string cluster = http_uri.cluster();
    const auto thread_local_cluster = context().clusterManager().getThreadLocalCluster(cluster);
    if (thread_local_cluster) {
      active_request_ =
          thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);
    } else {
      ENVOY_LOG(error, "ext_proc cluster {} does not exist in the config", cluster);
    }
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
      } else {
        ENVOY_LOG(error, "Response body is empty");
        onError();
        return;
      }
      if (callbacks_) {
        callbacks_->onComplete(response_msg);
        callbacks_ = nullptr;
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
    callbacks_ = nullptr;
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
