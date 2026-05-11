#include "source/extensions/filters/http/gcp_authn/jwt_gcp_authn_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

namespace {
constexpr absl::string_view UrlString =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/"
    "identity?audience=[AUDIENCE]";
constexpr char MetadataFlavorKey[] = "Metadata-Flavor";
constexpr char MetadataFlavor[] = "Google";

Http::RequestMessagePtr buildRequest(absl::string_view url) {
  absl::string_view host;
  absl::string_view path;
  Envoy::Http::Utility::extractHostPathFromUri(url, host, path);
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, "GET"},
           {Envoy::Http::Headers::get().Host, std::string(host)},
           {Envoy::Http::Headers::get().Path, std::string(path)},
           {Envoy::Http::LowerCaseString(MetadataFlavorKey), MetadataFlavor}});

  return std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
}
} // namespace

void JwtGcpAuthnClientImpl::fetchToken(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    GcpAuthnClient::Callbacks& callbacks) {
  // Cancel any active requests.
  cancel();
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  const std::string cluster =
      config_.cluster().empty() ? config_.http_uri().cluster() : config_.cluster();
  const auto thread_local_cluster =
      context_.serverFactoryContext().clusterManager().getThreadLocalCluster(cluster);

  // Failed to fetch the token if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    onError(absl::StrFormat("Failed to fetch the token: [cluster = %s] is not found or configured.", cluster));
    return;
  }

  // Set up the request options.
  Envoy::Http::AsyncClient::RequestOptions options =
      Envoy::Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
              config_.has_timeout() ? config_.timeout() : config_.http_uri().timeout())))
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

  std::string final_url = absl::StrReplaceAll(UrlString, {{"[AUDIENCE]", audience.url()}});
  active_request_ =
      thread_local_cluster->httpAsyncClient().send(buildRequest(final_url), *this, options);
}

void JwtGcpAuthnClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                      Http::ResponseMessagePtr&& response) {
  auto status = Envoy::Http::Utility::getResponseStatusOrNullopt(response->headers());
  active_request_ = nullptr;
  if (status.has_value()) {
    uint64_t status_code = status.value();
    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      ASSERT(callbacks_ != nullptr);
      callbacks_->onComplete(response->bodyAsString());
      callbacks_ = nullptr;
    } else {
      onError(absl::StrFormat("Response status is not OK, status: %d", status_code));
    }
  } else {
    // This occurs if the response headers are invalid.
    onError("Failed to get the response because response headers are not valid.");
  }
}

void JwtGcpAuthnClientImpl::onFailure(const Http::AsyncClient::Request&,
                                      Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset ||
         reason == Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
  active_request_ = nullptr;
  onError(absl::StrFormat("Request failed with reason: %d", enumToInt(reason)));
}

void JwtGcpAuthnClientImpl::cancel() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }
}

void JwtGcpAuthnClientImpl::onError(absl::string_view error_msg) {
  ENVOY_LOG(error, "{}", error_msg);

  // Cancel if the request is active.
  cancel();

  ASSERT(callbacks_ != nullptr);
  callbacks_->onComplete(absl::InternalError(error_msg));
  callbacks_ = nullptr;
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
