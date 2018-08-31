#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "envoy/http/async_client.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Object to implement Authenticator interface. It only processes one token.
 */
class AuthenticatorImpl : public Logger::Loggable<Logger::Id::filter>,
                          public Authenticator,
                          public Http::AsyncClient::Callbacks {
public:
  AuthenticatorImpl(FilterConfigSharedPtr config) : config_(config) {}

  // Following functions are for Authenticator interface
  void verify(Http::HeaderMap& headers, Authenticator::Callbacks* callback) override;
  void onDestroy() override;
  void sanitizePayloadHeaders(Http::HeaderMap& headers) const override;

private:
  // Fetch a remote public key.
  void fetchRemoteJwks();

  // Following two functions are for AyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  // Verify with a specific public key.
  void verifyKey();

  // Handle the public key fetch done event.
  void onFetchRemoteJwksDone(const std::string& jwks_str);

  // Calls the callback with status.
  void doneWithStatus(const Status& status);

  // Return true if it is OK to forward this request without JWT.
  bool okToBypass() const;

  // The config object.
  FilterConfigSharedPtr config_;

  // The token data
  JwtLocationConstPtr token_;
  // The JWT object.
  ::google::jwt_verify::Jwt jwt_;
  // The JWKS data object
  JwksCache::JwksData* jwks_data_{};

  // The HTTP request headers
  Http::HeaderMap* headers_{};
  // The on_done function.
  Authenticator::Callbacks* callback_{};

  // The pending uri_, only used for logging.
  std::string uri_;
  // The pending remote request so it can be canceled.
  Http::AsyncClient::Request* request_{};
};

void AuthenticatorImpl::sanitizePayloadHeaders(Http::HeaderMap& headers) const {
  for (const auto& it : config_->getProtoConfig().providers()) {
    const auto& provider = it.second;
    if (!provider.forward_payload_header().empty()) {
      headers.remove(Http::LowerCaseString(provider.forward_payload_header()));
    }
  }
}

void AuthenticatorImpl::verify(Http::HeaderMap& headers, Authenticator::Callbacks* callback) {
  headers_ = &headers;
  callback_ = callback;

  ENVOY_LOG(debug, "Jwt authentication starts");
  auto tokens = config_->getExtractor().extract(headers);
  if (tokens.empty()) {
    if (okToBypass()) {
      doneWithStatus(Status::Ok);
    } else {
      doneWithStatus(Status::JwtMissed);
    }
    return;
  }

  // TODO(qiwzhang), add supports for multiple tokens.
  // Only process the first token for now.
  token_.swap(tokens[0]);

  const Status status = jwt_.parseFromString(token_->token());
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  // Check if token is extracted from the location specified by the issuer.
  if (!token_->isIssuerSpecified(jwt_.iss_)) {
    ENVOY_LOG(debug, "Jwt for issuer {} is not extracted from the specified locations", jwt_.iss_);
    doneWithStatus(Status::JwtUnknownIssuer);
    return;
  }

  // Check "exp" claim.
  const auto unix_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  // NOTE: Service account tokens generally don't have an expiration time (due to being long lived)
  // and defaulted to 0 by google::jwt_verify library but are still valid.
  if (jwt_.exp_ > 0 && jwt_.exp_ < unix_timestamp) {
    doneWithStatus(Status::JwtExpired);
    return;
  }

  // Check the issuer is configured or not.
  jwks_data_ = config_->getCache().getJwksCache().findByIssuer(jwt_.iss_);
  // isIssuerSpecified() check already make sure the issuer is in the cache.
  ASSERT(jwks_data_ != nullptr);

  // Check if audience is allowed
  if (!jwks_data_->areAudiencesAllowed(jwt_.audiences_)) {
    doneWithStatus(Status::JwtAudienceNotAllowed);
    return;
  }

  if (jwks_data_->getJwksObj() != nullptr && !jwks_data_->isExpired()) {
    verifyKey();
    return;
  }

  // TODO(qiwzhang): potential optimization.
  // If request 1 triggers a remote jwks fetching, but is not yet replied when the request 2
  // of using the same jwks comes. The request 2 will trigger another remote fetching for the
  // jwks. This can be optimized; the same remote jwks fetching can be shared by two requrests.
  fetchRemoteJwks();
}

void AuthenticatorImpl::fetchRemoteJwks() {
  const auto& http_uri = jwks_data_->getJwtProvider().remote_jwks().http_uri();

  Http::MessagePtr message = Http::Utility::prepareHeaders(http_uri);
  message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);

  if (config_->cm().get(http_uri.cluster()) == nullptr) {
    doneWithStatus(Status::JwksFetchFail);
    return;
  }

  uri_ = http_uri.uri();
  ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", uri_);
  request_ = config_->cm()
                 .httpAsyncClientForCluster(http_uri.cluster())
                 .send(std::move(message), *this,
                       std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(http_uri.timeout())));
}

void AuthenticatorImpl::onSuccess(Http::MessagePtr&& response) {
  request_ = nullptr;
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(debug, "fetch pubkey [uri = {}]: success", uri_);
    if (response->body()) {
      const auto len = response->body()->length();
      const auto body = std::string(static_cast<char*>(response->body()->linearize(len)), len);
      onFetchRemoteJwksDone(body);
      return;
    } else {
      ENVOY_LOG(debug, "fetch pubkey [uri = {}]: body is empty", uri_);
    }
  } else {
    ENVOY_LOG(debug, "fetch pubkey [uri = {}]: response status code {}", uri_, status_code);
  }
  doneWithStatus(Status::JwksFetchFail);
}

void AuthenticatorImpl::onFailure(Http::AsyncClient::FailureReason) {
  request_ = nullptr;
  ENVOY_LOG(debug, "fetch pubkey [uri = {}]: failed", uri_);
  doneWithStatus(Status::JwksFetchFail);
}

void AuthenticatorImpl::onDestroy() {
  if (request_ != nullptr) {
    request_->cancel();
    request_ = nullptr;
    ENVOY_LOG(debug, "fetch pubkey [uri = {}]: canceled", uri_);
  }
}

// Handle the public key fetch done event.
void AuthenticatorImpl::onFetchRemoteJwksDone(const std::string& jwks_str) {
  const Status status = jwks_data_->setRemoteJwks(jwks_str);
  if (status != Status::Ok) {
    doneWithStatus(status);
  } else {
    verifyKey();
  }
}

// Verify with a specific public key.
void AuthenticatorImpl::verifyKey() {
  const Status status = ::google::jwt_verify::verifyJwt(jwt_, *jwks_data_->getJwksObj());
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  // Forward the payload
  const auto& provider = jwks_data_->getJwtProvider();
  if (!provider.forward_payload_header().empty()) {
    headers_->addCopy(Http::LowerCaseString(provider.forward_payload_header()),
                      jwt_.payload_str_base64url_);
  }

  if (!provider.forward()) {
    // Remove JWT from headers.
    token_->removeJwt(*headers_);
  }

  doneWithStatus(Status::Ok);
}

bool AuthenticatorImpl::okToBypass() const {
  // TODO(qiwzhang): use requirement field
  return false;
}

void AuthenticatorImpl::doneWithStatus(const Status& status) {
  ENVOY_LOG(debug, "Jwt authentication completed with: {}",
            ::google::jwt_verify::getStatusString(status));
  callback_->onComplete(status);
  callback_ = nullptr;
}

} // namespace

AuthenticatorPtr Authenticator::create(FilterConfigSharedPtr config) {
  return std::make_unique<AuthenticatorImpl>(config);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
