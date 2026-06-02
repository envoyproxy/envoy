#include "source/extensions/filters/http/gcp_authn/gcp_authn_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/verify.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

namespace {
constexpr absl::string_view JwtTokenEndpoint =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/"
    "identity?audience=[AUDIENCE]";
constexpr absl::string_view AccessTokenEndpoint =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/"
    "token";
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

absl::StatusOr<GcpToken>
parseJwtResponse(const std::string& response_body,
                 const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience) {
  JwtVerify::Jwt jwt;
  if (jwt.parseFromString(response_body) == JwtVerify::Status::Ok) {
    return GcpToken{response_body, jwt.exp_, audience};
  }
  return absl::InternalError("Failed to parse identity token/JWT.");
}

absl::StatusOr<GcpToken>
parseAccessTokenResponse(const std::string& response_body,
                         const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                         TimeSource& time_source) {
  auto json_or_error = Json::Factory::loadFromString(response_body);
  if (!json_or_error.ok()) {
    return absl::InternalError("Failed to parse access token response as JSON.");
  }
  auto json = json_or_error.value();
  auto access_token_or_error = json->getString("access_token");
  auto expires_in_or_error = json->getInteger("expires_in");
  if (!access_token_or_error.ok() || !expires_in_or_error.ok()) {
    return absl::InternalError("Failed to extract access_token or expires_in from response.");
  }
  std::string access_token = access_token_or_error.value();
  int64_t expires_in = expires_in_or_error.value();
  if (access_token.empty()) {
    return absl::InternalError("Extracted access_token is empty.");
  }
  if (expires_in <= 0) {
    return absl::InternalError("Extracted expires_in is non-positive.");
  }
  uint64_t expires_at = DateUtil::nowToSeconds(time_source) + expires_in;
  return GcpToken{access_token, expires_at, audience};
}
} // namespace

void GcpAuthnClientImpl::fetchToken(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    GcpAuthnClient::Callbacks& callbacks) {
  // Cancel any active requests.
  cancel();
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  audience_ = audience;

  const std::string cluster =
      config_.cluster().empty() ? config_.http_uri().cluster() : config_.cluster();
  const auto thread_local_cluster =
      context_.serverFactoryContext().clusterManager().getThreadLocalCluster(cluster);

  // Failed to fetch the token if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    onError(absl::StrFormat("Failed to fetch the token: [cluster = %s] is not found or configured.",
                            cluster));
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

  std::string final_url;
  if (audience.has_access_token()) {
    token_type_ = TokenType::AccessToken;
    final_url = AccessTokenEndpoint;
  } else if (!audience.url().empty()) {
    token_type_ = TokenType::Jwt;
    final_url = absl::StrReplaceAll(JwtTokenEndpoint, {{"[AUDIENCE]", audience.url()}});
  } else {
    onError("Failed to fetch the token: both url and access_token are empty.");
    return;
  }
  active_request_ =
      thread_local_cluster->httpAsyncClient().send(buildRequest(final_url), *this, options);
}

void GcpAuthnClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                   Http::ResponseMessagePtr&& response) {
  auto status = Envoy::Http::Utility::getResponseStatusOrNullopt(response->headers());
  active_request_ = nullptr;
  if (!status.has_value()) {
    // This occurs if the response headers are invalid.
    onError("Failed to get the response because response headers are not valid.");
    return;
  }

  uint64_t status_code = status.value();
  if (status_code != Envoy::enumToInt(Envoy::Http::Code::OK)) {
    onError(absl::StrFormat("Response status is not OK, status: %d", status_code));
    return;
  }

  ASSERT(callbacks_ != nullptr);
  std::string response_body = response->bodyAsString();
  absl::StatusOr<GcpToken> token_or_error;
  if (token_type_ == TokenType::Jwt) {
    token_or_error = parseJwtResponse(response_body, audience_);
  } else {
    token_or_error = parseAccessTokenResponse(response_body, audience_,
                                              context_.serverFactoryContext().timeSource());
  }

  if (token_or_error.ok()) {
    callbacks_->onComplete(token_or_error.value());
  } else {
    onError(token_or_error.status().message());
    return;
  }
  callbacks_ = nullptr;
}

void GcpAuthnClientImpl::onFailure(const Http::AsyncClient::Request&,
                                   Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset ||
         reason == Http::AsyncClient::FailureReason::ExceedResponseBufferLimit);
  active_request_ = nullptr;
  onError(absl::StrFormat("Request failed with reason: %d", enumToInt(reason)));
}

void GcpAuthnClientImpl::cancel() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }
}

void GcpAuthnClientImpl::onError(absl::string_view error_msg) {
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
