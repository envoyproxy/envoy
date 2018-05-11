#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "envoy/http/async_client.h"

#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

// from jwt_verify library
#include "jwt.h"
#include "verify.h"

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
  AuthenticatorImpl(Upstream::ClusterManager& cm, DataStore& store) : cm_(cm), store_(store) {}

  // Following functions are for Authenticator interface
  void verify(Http::HeaderMap& headers, Authenticator::Callbacks* callback) override;
  void onDestroy() override;
  void sanitizePayloadHeaders(Http::HeaderMap& headers) override;

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
  bool okToBypass();

  // The cluster manager object to make HTTP call.
  Upstream::ClusterManager& cm_;
  // The cache object.
  DataStore& store_;

  // The token data
  JwtLocationConstPtr token_;
  // The JWT object.
  ::google::jwt_verify::Jwt jwt_;
  // The jwks data object
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

void AuthenticatorImpl::sanitizePayloadHeaders(Http::HeaderMap& headers) {
  for (const auto& rule : store_.config().rules()) {
    if (!rule.forward_payload_header().empty()) {
      headers.remove(Http::LowerCaseString(rule.forward_payload_header()));
    }
  }
}

void AuthenticatorImpl::verify(Http::HeaderMap& headers, Authenticator::Callbacks* callback) {
  headers_ = &headers;
  callback_ = callback;

  ENVOY_LOG(debug, "Jwt authentication starts");
  auto tokens = store_.getExtractor().extract(headers);
  if (tokens.size() == 0) {
    if (okToBypass()) {
      doneWithStatus(Status::Ok);
    } else {
      doneWithStatus(Status::JwtMissed);
    }
    return;
  }

  // TODO, add supports for multiple tokens.
  // Only process the first token for now.
  token_.swap(tokens[0]);

  Status status = jwt_.parseFromString(token_->token());
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
  if (jwt_.exp_ < unix_timestamp) {
    doneWithStatus(Status::JwtExpired);
    return;
  }

  // Check the issuer is configured or not.
  jwks_data_ = store_.getJwksCache().findByIssuer(jwt_.iss_);
  if (jwks_data_ == nullptr) {
    doneWithStatus(Status::JwtUnknownIssuer);
    return;
  }

  // Check if audience is allowed
  if (!jwks_data_->areAudiencesAllowed(jwt_.audiences_)) {
    doneWithStatus(Status::JwtAudienceNotAllowed);
    return;
  }

  if (jwks_data_->getJwksObj() != nullptr && !jwks_data_->isExpired()) {
    verifyKey();
    return;
  }

  fetchRemoteJwks();
}

void AuthenticatorImpl::fetchRemoteJwks() {
  uri_ = jwks_data_->getJwtRule().remote_jwks().http_uri().uri();
  std::string host, path;
  Http::Utility::extractHostPathFromUri(uri_, host, path);

  Http::MessagePtr message(new Http::RequestMessageImpl());
  message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
  message->headers().insertPath().value(path);
  message->headers().insertHost().value(host);

  const auto& cluster = jwks_data_->getJwtRule().remote_jwks().http_uri().cluster();
  if (cm_.get(cluster) == nullptr) {
    doneWithStatus(Status::JwksFetchFail);
    return;
  }

  ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", uri_);
  request_ = cm_.httpAsyncClientForCluster(cluster).send(
      std::move(message), *this, absl::optional<std::chrono::milliseconds>());
}

void AuthenticatorImpl::onSuccess(Http::MessagePtr&& response) {
  request_ = nullptr;
  uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == 200) {
    ENVOY_LOG(debug, "fetch pubkey [uri = {}]: success", uri_);
    std::string body;
    if (response->body()) {
      auto len = response->body()->length();
      body = std::string(static_cast<char*>(response->body()->linearize(len)), len);
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
  if (request_) {
    request_->cancel();
    request_ = nullptr;
    ENVOY_LOG(debug, "fetch pubkey [uri = {}]: canceled", uri_);
  }
}

// Handle the public key fetch done event.
void AuthenticatorImpl::onFetchRemoteJwksDone(const std::string& jwks_str) {
  Status status = jwks_data_->setRemoteJwks(jwks_str);
  if (status != Status::Ok) {
    doneWithStatus(status);
  } else {
    verifyKey();
  }
}

// Verify with a specific public key.
void AuthenticatorImpl::verifyKey() {
  Status status = ::google::jwt_verify::verifyJwt(jwt_, *jwks_data_->getJwksObj());
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  // Forward the payload
  const auto& rule = jwks_data_->getJwtRule();
  if (!rule.forward_payload_header().empty()) {
    headers_->addCopy(Http::LowerCaseString(rule.forward_payload_header()),
                      jwt_.payload_str_base64url_);
  }

  if (!rule.forward()) {
    // Remove JWT from headers.
    token_->removeJwt(*headers_);
  }

  doneWithStatus(Status::Ok);
}

bool AuthenticatorImpl::okToBypass() {
  if (store_.config().allow_missing_or_failed()) {
    return true;
  }

  // TODO: use bypass field
  return false;
}

void AuthenticatorImpl::doneWithStatus(const Status& status) {
  ENVOY_LOG(debug, "Jwt authentication completed with: {}",
            ::google::jwt_verify::getStatusString(status));
  if (status != Status::Ok && store_.config().allow_missing_or_failed()) {
    callback_->onComplete(Status::Ok);
  } else {
    callback_->onComplete(status);
  }
  callback_ = nullptr;
}

} // namespace

AuthenticatorPtr Authenticator::create(DataStoreFactorySharedPtr store_factory) {
  return AuthenticatorPtr(new AuthenticatorImpl(store_factory->cm(), store_factory->store()));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
