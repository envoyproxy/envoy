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

using ::google::jwt_verify::Status;
using Http::FilterHeadersStatus;

constexpr char MetadataFlavorKey[] = "Metadata-Flavor";
constexpr char MetadataFlavor[] = "Google";

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  client_->fetchToken();
  return FilterHeadersStatus::Continue;
}

Http::RequestMessagePtr buildRequest(const std::string& method, const std::string& server_url) {
  absl::string_view host;
  absl::string_view path;
  Envoy::Http::Utility::extractHostPathFromUri(server_url, host, path);
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, method},
           {Envoy::Http::Headers::get().Host, std::string(host)},
           {Envoy::Http::Headers::get().Path, std::string(path)},
           {Envoy::Http::LowerCaseString(MetadataFlavorKey), MetadataFlavor}});

  return std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
}

// TODO(tyxia) Pass the return of buildRequest to the fetchToken??
void GcpAuthnClient::fetchToken() {
  resetRequest();

  const auto thread_local_cluster =
      context_.clusterManager().getThreadLocalCluster(config_.http_uri().cluster());

  // Fail the request if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error,
              "Failed to fetch the token [uri = {}]: [cluster = {}] is not found or configured.",
              config_.http_uri().uri(), config_.http_uri().cluster());
    // TODO(tyxia)
    // handleFailure here
    handleFailure();
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

  Http::RequestMessagePtr request = buildRequest("GET", config_.http_uri().uri());
  active_request_ =
      thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
}

void GcpAuthnClient::onSuccess(const Http::AsyncClient::Request&,
                               Http::ResponseMessagePtr&& response) {
  try {
    const uint64_t status_code = Envoy::Http::Utility::getResponseStatus(response->headers());

    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      // Decode JWT Token
      ::google::jwt_verify::Jwt jwt;
      Status status = jwt.parseFromString(response->bodyAsString());
      if (status == Status::Ok) {
      }
    } else {
      ENVOY_LOG(error, "Failed to get the response status: {}", status_code);
      handleFailure();
    }
  } catch (const Envoy::EnvoyException& e) {
    // This occurs if the response headers are invalid.
    ENVOY_LOG(error, "Failed to get the response: {}", e.what());
    handleFailure();
  }
}

void GcpAuthnClient::onFailure(const Http::AsyncClient::Request&,
                               Http::AsyncClient::FailureReason reason) {

  if (reason == Http::AsyncClient::FailureReason::Reset) {
    ENVOY_LOG(error, "Failed to fetch the token [uri = {}]: the stream has been reset",
              config_.http_uri().uri());
  } else {
    ENVOY_LOG(debug, "Failed to fetch the token [uri = {}]: failed network error {}",
              config_.http_uri().uri(), enumToInt(reason));
  }

  handleFailure();
}

void GcpAuthnClient::handleFailure() {
  // TODO(tyxia) Add logs here.
  resetRequest();
}

} // namespace GcpAuthentication
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
