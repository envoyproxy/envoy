#include "source/extensions/filters/http/custom_response/custom_response_filter.h"

#include "envoy/http/filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

void CustomResponseFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus CustomResponseFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  (void)end_stream;
  auto custom_response = config_->getResponse(headers, encoder_callbacks_->streamInfo());

  // A valid custom response was not found. We should just pass through.
  if (!custom_response) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Handle remote body
  if (custom_response->isRemote()) {
    auto& remote_data_source = custom_response->remoteDataSource();
    ASSERT(remote_data_source.has_value());
    client_ = std::make_unique<RemoteResponseClient>(*remote_data_source, factory_context_);
    client_->fetchBody(
        [this, custom_response, &headers](const Http::ResponseMessage* client_response) {
          onRemoteResponse(headers, custom_response, client_response);
        });
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Handle local body
  std::string body;
  Http::Code code;
  custom_response->rewrite(headers, encoder_callbacks_->streamInfo(), body, code);
  encoder_callbacks_->sendLocalReply(code, "", nullptr, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

void CustomResponseFilter::onRemoteResponse(Http::ResponseHeaderMap& headers,
                                            const ResponseSharedPtr& custom_response,
                                            const Http::ResponseMessage* client_response) {
  if (client_response != nullptr) {
    auto body = client_response->bodyAsString();
    Http::Code code;
    custom_response->rewrite(headers, encoder_callbacks_->streamInfo(), body, code);
    encoder_callbacks_->sendLocalReply(code, "", nullptr, absl::nullopt, "");
  } else {
    stats_.get_remote_response_failed_.inc();
  }
  encoder_callbacks_->continueEncoding();
}

void RemoteResponseClient::fetchBody(RequestCB callback) {
  // Cancel any active requests.
  cancel();
  ASSERT(callback_ == nullptr);
  callback_ = std::move(callback);

  // Construct the request
  absl::string_view host;
  absl::string_view path;
  Envoy::Http::Utility::extractHostPathFromUri(config_.http_uri().uri(), host, path);
  Http::RequestMessagePtr request = std::make_unique<Envoy::Http::RequestMessageImpl>(
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, "GET"},
           {Envoy::Http::Headers::get().Host, std::string(host)},
           {Envoy::Http::Headers::get().Path, std::string(path)}}));

  const std::string cluster = config_.http_uri().cluster();
  const std::string uri = config_.http_uri().uri();
  const auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(cluster);

  // Failed to fetch the token if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "Failed to fetch the token: [cluster = {}] is not found or configured.",
              cluster);
    onError();
    return;
  }

  // Set up the request options.
  struct Envoy::Http::AsyncClient::RequestOptions options =
      Envoy::Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(
          DurationUtil::durationToMilliseconds(config_.http_uri().timeout())));

  if (config_.has_retry_policy()) {
    route_retry_policy_ = Http::Utility::convertCoreToRouteRetryPolicy(
        config_.retry_policy(), "5xx,gateway-error,connect-failure,reset");
    options.setRetryPolicy(route_retry_policy_);
    options.setBufferBodyForRetry(true);
  }

  active_request_ =
      thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
}

void RemoteResponseClient::onSuccess(const Http::AsyncClient::Request&,
                                     Http::ResponseMessagePtr&& response) {

  auto status = Envoy::Http::Utility::getResponseStatusOrNullopt(response->headers());
  active_request_ = nullptr;
  if (status.has_value()) {
    uint64_t status_code = status.value();
    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      ASSERT(callback_ != nullptr);
      callback_(response.get());
      callback_ = nullptr;
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

void RemoteResponseClient::onFailure(const Http::AsyncClient::Request&,
                                     Http::AsyncClient::FailureReason reason) {
  // Http::AsyncClient::FailureReason only has one value: "Reset".
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  ENVOY_LOG(error, "Request failed: stream has been reset");
  active_request_ = nullptr;
  onError();
}

void RemoteResponseClient::cancel() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }
}

void RemoteResponseClient::onError() {
  // Cancel if the request is active.
  cancel();

  ASSERT(callback_ != nullptr);
  callback_(/*response_ptr=*/nullptr);
  callback_ = nullptr;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
