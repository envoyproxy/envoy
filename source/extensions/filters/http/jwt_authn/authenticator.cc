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
                          public Common::JwksFetcher::JwksReceiver {
public:
  AuthenticatorImpl(FilterConfigSharedPtr config, CreateJwksFetcherCb createJwksFetcherCb)
      : config_(config), createJwksFetcherCb_(createJwksFetcherCb) {}

  // Following functions are for JwksFetcher::JwksReceiver interface
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override;
  void onJwksError(Failure reason) override;
  // Following functions are for Authenticator interface
  void verify(Http::HeaderMap& headers, Authenticator::Callbacks* callback) override;
  void onDestroy() override;
  void sanitizePayloadHeaders(Http::HeaderMap& headers) const override;

  TimeSource& timeSource() { return config_->timeSource(); }

private:
  // Verify with a specific public key.
  void verifyKey();

  // Calls the callback with status.
  void doneWithStatus(const Status& status);

  // Return true if it is OK to forward this request without JWT.
  bool okToBypass() const;

  // The config object.
  FilterConfigSharedPtr config_;

  // The callback used to create a JwksFetcher instance.
  CreateJwksFetcherCb createJwksFetcherCb_;

  // The Jwks fetcher object
  Common::JwksFetcherPtr fetcher_;

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
  ASSERT(!callback_);
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

  // TODO(qiwzhang): Cross-platform-wise the below unix_timestamp code is wrong as the
  // epoch is not guaranteed to be defined as the unix epoch. We should use
  // the abseil time functionality instead or use the jwt_verify_lib to check
  // the validity of a JWT.
  // Check "exp" claim.
  const auto unix_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(timeSource().systemTime().time_since_epoch())
          .count();
  // If the nbf claim does *not* appear in the JWT, then the nbf field is defaulted
  // to 0.
  if (jwt_.nbf_ > unix_timestamp) {
    doneWithStatus(Status::JwtNotYetValid);
    return;
  }
  // If the exp claim does *not* appear in the JWT then the exp field is defaulted
  // to 0.
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

  auto jwks_obj = jwks_data_->getJwksObj();
  if (jwks_obj != nullptr && !jwks_data_->isExpired()) {
    // TODO(qiwzhang): It would seem there's a window of error whereby if the JWT issuer
    // has started signing with a new key that's not in our cache, then the
    // verification will fail even though the JWT is valid. A simple fix
    // would be to check the JWS kid header field; if present check we have
    // the key cached, if we do proceed to verify else try a new JWKS retrieval.
    // JWTs without a kid header field in the JWS we might be best to get each
    // time? This all only matters for remote JWKS.
    verifyKey();
    return;
  }

  // TODO(qiwzhang): potential optimization.
  // If request 1 triggers a remote jwks fetching, but is not yet replied when the request 2
  // of using the same jwks comes. The request 2 will trigger another remote fetching for the
  // jwks. This can be optimized; the same remote jwks fetching can be shared by two requrests.
  if (jwks_data_->getJwtProvider().has_remote_jwks()) {
    if (!fetcher_) {
      fetcher_ = createJwksFetcherCb_(config_->cm());
    }
    fetcher_->fetch(jwks_data_->getJwtProvider().remote_jwks().http_uri(), *this);
  } else {
    // No valid keys for this issuer. This may happen as a result of incorrect local
    // JWKS configuration.
    doneWithStatus(Status::JwksNoValidKeys);
  }
}

void AuthenticatorImpl::onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) {
  const Status status = jwks_data_->setRemoteJwks(std::move(jwks))->getStatus();
  if (status != Status::Ok) {
    doneWithStatus(status);
  } else {
    verifyKey();
  }
}

void AuthenticatorImpl::onJwksError(Failure) { doneWithStatus(Status::JwksFetchFail); }

void AuthenticatorImpl::onDestroy() {
  if (fetcher_) {
    fetcher_->cancel();
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

AuthenticatorPtr Authenticator::create(FilterConfigSharedPtr config,
                                       CreateJwksFetcherCb createJwksFetcherCb) {
  return std::make_unique<AuthenticatorImpl>(config, createJwksFetcherCb);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
