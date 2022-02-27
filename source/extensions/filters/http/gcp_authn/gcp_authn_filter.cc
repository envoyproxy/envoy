#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthentication {

// TODO(tyxia) Using is outside or inside
using Http::FilterHeadersStatus;
using ::google::jwt_verify::Status;

constexpr char kMetadataFlavorKey[] = "Metadata-Flavor";
constexpr char kMetadataFlavor[] = "Google";

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // TODO(tyxia) This is on the worker thread
  // Think about where to call this function.
  auto client = CreateGcpAuthnClient();
  client->sendRequest();
  return FilterHeadersStatus::Continue;
}

Http::RequestMessagePtr GcpAuthnClient::buildRequest(const std::string& method) {
  absl::string_view host;
  absl::string_view path;
  absl::string_view server_url = config_.http_uri().uri();
  Envoy::Http::Utility::extractHostPathFromUri(server_url, host, path);
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, method},
           {Envoy::Http::Headers::get().Host, std::string(host)},
           {Envoy::Http::Headers::get().Path, std::string(path)},
           {Envoy::Http::LowerCaseString(kMetadataFlavorKey), kMetadataFlavor}});

  return std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
}

void GcpAuthnClient::sendRequest() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }

  const auto thread_local_cluster =
      context_.clusterManager().getThreadLocalCluster(config_.http_uri().cluster());

  // Fail the request if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "{}: send request [uri = {}] failed: [cluster = {}] is not configured",
              __func__, config_.http_uri().uri(), config_.http_uri().cluster());
    // TODO(tyxia)
    // Something like onFailure()
    // Or common function in the called from onFailure
    return;
  }

  // Set up the request options.
  struct Envoy::Http::AsyncClient::RequestOptions options =
      Envoy::Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(config_.http_uri().timeout())))
          // GCP metadata server rejects X-Forwarded-For requests.
          // https://cloud.google.com/compute/docs/storing-retrieving-metadata#x-forwarded-for_header
          .setSendXff(false);
  if (config_.has_retry_policy()) {
    envoy::config::route::v3::RetryPolicy route_retry_policy =
        Http::Utility::convertCoreToRouteRetryPolicy(config_.retry_policy(),
                                                     "5xx,gateway-error,connect-failure,reset");
    options.setRetryPolicy(route_retry_policy);
    options.setBufferBodyForRetry(true);
  }

  Http::RequestMessagePtr request = buildRequest("GET");
  active_request_ =
      thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
}

void GcpAuthnClient::onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  ProcessResponse(std::move(response));
}

void GcpAuthnClient::onFailure(const Http::AsyncClient::Request&,
                               Http::AsyncClient::FailureReason reason) {

  if (reason == Http::AsyncClient::FailureReason::Reset) {
    ENVOY_LOG(debug, "{}: fetch token [uri = {}] failed: the stream has been reset}", __func__,
              config_.http_uri().uri());
  } else {
    ENVOY_LOG(debug, "{}: fetch token [uri = {}]: failed network error {}", __func__,
              config_.http_uri().uri(), enumToInt(reason));
  }

  // HandleFailedResponse and retry ??
}

void GcpAuthnClient::ProcessResponse(Http::ResponseMessagePtr&& response) {
  try {
    const uint64_t status_code =
        Envoy::Http::Utility::getResponseStatus(response->headers());

    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      // Decode JWT Token
      ::google::jwt_verify::Jwt jwt;
      Status status = jwt.parseFromString(response->bodyAsString());
      if (status == Status::Ok) {
        uint64_t exp_time = jwt.exp_;
        // TODO(tyxia) Test code
        std::cout << exp_time << std::endl;
      }

    } else {
      ENVOY_LOG(error, "{}: failed: {}", status_code);
      // handleFailResponse();
    }
  } catch (const Envoy::EnvoyException& e) {
    // This occurs if the reponse headers are invalid.
    ENVOY_LOG(error, "Failed to get the response: {}" , e.what());
  }
}

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
