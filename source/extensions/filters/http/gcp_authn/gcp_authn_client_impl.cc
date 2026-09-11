#include "source/extensions/filters/http/gcp_authn/gcp_authn_client_impl.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/verify.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

namespace {
constexpr absl::string_view DefaultServiceAccountPrefix =
    "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/";
constexpr absl::string_view IdentityUrlPath = "identity";
constexpr absl::string_view TokenUrlPath = "token";
constexpr absl::string_view AudienceQueryKey = "audience";
constexpr char MetadataFlavorKey[] = "Metadata-Flavor";
constexpr char MetadataFlavor[] = "Google";
constexpr char BindCertificateFingerprintKey[] = "bindCertificateFingerprint";

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

Http::RequestMessagePtr
buildIamPostRequest(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                    const std::string& authorization) {
  Http::RequestHeaderMapPtr headers =
      Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Method, "POST"},
           {Envoy::Http::Headers::get().Host, "iamcredentials.googleapis.com"},
           {Envoy::Http::Headers::get().Path,
            absl::StrCat("/v1/projects/-/serviceAccounts/", audience.iam_access_token().account(),
                         ":generateAccessToken")},
           {Envoy::Http::CustomHeaders::get().Authorization, authorization},
           {Envoy::Http::Headers::get().ContentType, "application/json; charset=utf-8"}});

  Protobuf::Struct request_body_struct;
  auto* fields = request_body_struct.mutable_fields();
  auto* list_value = (*fields)["scope"].mutable_list_value();
  if (audience.iam_access_token().scopes().empty()) {
    list_value->add_values()->set_string_value("https://www.googleapis.com/auth/cloud-platform");
  } else {
    for (const auto& scope : audience.iam_access_token().scopes()) {
      list_value->add_values()->set_string_value(scope);
    }
  }
  (*fields)["lifetime"].set_string_value("3600s");
  std::string body;
  std::ignore = Protobuf::util::MessageToJsonString(request_body_struct, &body);

  auto message = std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
  message->body().add(body);
  return message;
}

absl::StatusOr<GcpToken>
parseJwtResponse(const std::string& response_body,
                 const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                 const std::optional<std::string>& fingerprint) {
  JwtVerify::Jwt jwt;
  if (jwt.parseFromString(response_body) == JwtVerify::Status::Ok) {
    return GcpToken{response_body, jwt.exp_, audience, fingerprint};
  }
  return absl::InternalError("Failed to parse identity token/JWT.");
}

absl::StatusOr<GcpToken>
parseAccessTokenResponse(const std::string& response_body,
                         const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                         const std::optional<std::string>& fingerprint, TimeSource& time_source) {
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
  return GcpToken{access_token, expires_at, audience, fingerprint};
}

absl::StatusOr<GcpToken> parseIamAccessTokenResponse(
    const std::string& response_body,
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience) {
  auto json_or_error = Json::Factory::loadFromString(response_body);
  if (!json_or_error.ok()) {
    return absl::InternalError("Failed to parse IAM access token response as JSON.");
  }
  auto json = json_or_error.value();
  auto access_token_or_error = json->getString("accessToken");
  auto expire_time_or_error = json->getString("expireTime");
  if (!access_token_or_error.ok() || !expire_time_or_error.ok()) {
    return absl::InternalError("Failed to extract accessToken or expireTime from response.");
  }
  std::string access_token = access_token_or_error.value();
  std::string expire_time = expire_time_or_error.value();
  if (access_token.empty()) {
    return absl::InternalError("Extracted accessToken is empty.");
  }
  Protobuf::Timestamp timestamp;
  if (!Protobuf::util::TimeUtil::FromString(expire_time, &timestamp)) {
    return absl::InternalError("Failed to parse expireTime timestamp.");
  }
  int64_t expires_at = Protobuf::util::TimeUtil::TimestampToSeconds(timestamp);
  if (expires_at <= 0) {
    return absl::InternalError("Parsed expireTime is non-positive.");
  }
  return GcpToken{access_token, static_cast<uint64_t>(expires_at), audience};
}
} // namespace

void GcpAuthnClientImpl::fetchUnboundJwt(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    GcpAuthnClient::Callbacks& callbacks) {
  Http::Utility::QueryParamsMulti query_params;
  query_params.add(AudienceQueryKey, audience.url());
  const std::string final_url =
      absl::StrCat(DefaultServiceAccountPrefix, IdentityUrlPath, query_params.toString());
  makeTokenRequest(TokenType::Jwt, audience, final_url, std::nullopt, callbacks);
}

void GcpAuthnClientImpl::fetchUnboundAccessToken(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    GcpAuthnClient::Callbacks& callbacks) {
  const std::string final_url = absl::StrCat(DefaultServiceAccountPrefix, TokenUrlPath);
  makeTokenRequest(TokenType::AccessToken, audience, final_url, std::nullopt, callbacks);
}

void GcpAuthnClientImpl::fetchBoundJwt(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    const std::string& fingerprint, GcpAuthnClient::Callbacks& callbacks) {
  Http::Utility::QueryParamsMulti query_params;
  query_params.add(AudienceQueryKey, audience.bound_jwt().url());
  // N.B.: double-URL-encoding is REQUIRED by the GCP metadata server.
  query_params.add(BindCertificateFingerprintKey,
                   Http::Utility::PercentEncoding::urlEncode(
                       Http::Utility::PercentEncoding::urlEncode(fingerprint)));
  const std::string final_url =
      absl::StrCat(DefaultServiceAccountPrefix, IdentityUrlPath, query_params.toString());
  makeTokenRequest(TokenType::BoundJwt, audience, final_url, fingerprint, callbacks);
}

void GcpAuthnClientImpl::fetchBoundAccessToken(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    const std::string& fingerprint, GcpAuthnClient::Callbacks& callbacks) {
  Http::Utility::QueryParamsMulti query_params;
  // N.B.: double-URL-encoding is REQUIRED by the GCP metadata server.
  query_params.add(BindCertificateFingerprintKey,
                   Http::Utility::PercentEncoding::urlEncode(
                       Http::Utility::PercentEncoding::urlEncode(fingerprint)));
  const std::string final_url =
      absl::StrCat(DefaultServiceAccountPrefix, TokenUrlPath, query_params.toString());
  makeTokenRequest(TokenType::BoundAccessToken, audience, final_url, fingerprint, callbacks);
}

void GcpAuthnClientImpl::fetchIamAccessToken(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    const std::string& authorization, GcpAuthnClient::Callbacks& callbacks) {
  sendRequest(TokenType::IamAccessToken, audience, std::nullopt,
              buildIamPostRequest(audience, authorization), callbacks);
}

void GcpAuthnClientImpl::makeTokenRequest(
    TokenType token_type, const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    const std::string& final_url, const std::optional<std::string>& fingerprint,
    GcpAuthnClient::Callbacks& callbacks) {
  sendRequest(token_type, audience, fingerprint, buildRequest(final_url), callbacks);
}

void GcpAuthnClientImpl::sendRequest(
    TokenType token_type, const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    const std::optional<std::string>& fingerprint, Http::RequestMessagePtr request,
    GcpAuthnClient::Callbacks& callbacks) {
  // Cancel any active requests.
  cancel();
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  audience_ = audience;
  fingerprint_ = fingerprint;

  const std::string cluster =
      config_.cluster().empty() ? config_.http_uri().cluster() : config_.cluster();
  const auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(cluster);

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

  token_type_ = token_type;
  active_request_ =
      thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
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
  if (token_type_ == TokenType::Jwt || token_type_ == TokenType::BoundJwt) {
    token_or_error = parseJwtResponse(response_body, audience_, fingerprint_);
  } else if (token_type_ == TokenType::IamAccessToken) {
    token_or_error = parseIamAccessTokenResponse(response_body, audience_);
  } else {
    token_or_error =
        parseAccessTokenResponse(response_body, audience_, fingerprint_, context_.timeSource());
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
  fingerprint_ = std::nullopt;
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
