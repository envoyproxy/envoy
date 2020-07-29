#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "envoy/http/async_client.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/tracing/http_tracer_impl.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

using ::google::jwt_verify::CheckAudience;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Object to implement Authenticator interface.
 */
class AuthenticatorImpl : public Logger::Loggable<Logger::Id::jwt>,
                          public Authenticator,
                          public Common::JwksFetcher::JwksReceiver {
public:
  AuthenticatorImpl(const CheckAudience* check_audience,
                    const absl::optional<std::string>& provider, bool allow_failed,
                    bool allow_missing, JwksCache& jwks_cache,
                    Upstream::ClusterManager& cluster_manager,
                    CreateJwksFetcherCb create_jwks_fetcher_cb, TimeSource& time_source)
      : jwks_cache_(jwks_cache), cm_(cluster_manager),
        create_jwks_fetcher_cb_(create_jwks_fetcher_cb), check_audience_(check_audience),
        provider_(provider), is_allow_failed_(allow_failed), is_allow_missing_(allow_missing),
        time_source_(time_source) {}

  // Following functions are for JwksFetcher::JwksReceiver interface
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override;
  void onJwksError(Failure reason) override;
  // Following functions are for Authenticator interface
  void verify(Http::HeaderMap& headers, Tracing::Span& parent_span,
              std::vector<JwtLocationConstPtr>&& tokens, SetPayloadCallback set_payload_cb,
              AuthenticatorCallback callback) override;
  void onDestroy() override;

  TimeSource& timeSource() { return time_source_; }

private:
  // Returns the name of the authenticator. For debug logging only.
  std::string name() const;

  // Verify with a specific public key.
  void verifyKey();

  // Calls the callback with status.
  void doneWithStatus(const Status& status);

  // Start verification process. It will continue to eliminate tokens with invalid claims until it
  // finds one to verify with key.
  void startVerify();

  // The jwks cache object.
  JwksCache& jwks_cache_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;

  // The callback used to create a JwksFetcher instance.
  CreateJwksFetcherCb create_jwks_fetcher_cb_;

  // The Jwks fetcher object
  Common::JwksFetcherPtr fetcher_;

  // The token data
  std::vector<JwtLocationConstPtr> tokens_;
  JwtLocationConstPtr curr_token_;
  // The JWT object.
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_;
  // The JWKS data object
  JwksCache::JwksData* jwks_data_{};

  // The HTTP request headers
  Http::HeaderMap* headers_{};
  // The active span for the request
  Tracing::Span* parent_span_{&Tracing::NullSpan::instance()};
  // the callback function to set payload
  SetPayloadCallback set_payload_cb_;
  // The on_done function.
  AuthenticatorCallback callback_;
  // check audience object.
  const CheckAudience* check_audience_;
  // specific provider or not when it is allow missing or failed.
  const absl::optional<std::string> provider_;
  const bool is_allow_failed_;
  const bool is_allow_missing_;
  TimeSource& time_source_;
};

std::string AuthenticatorImpl::name() const {
  if (provider_) {
    return provider_.value() + (is_allow_missing_ ? "-OPTIONAL" : "");
  }
  if (is_allow_failed_) {
    return "_IS_ALLOW_FAILED_";
  }
  if (is_allow_missing_) {
    return "_IS_ALLOW_MISSING_";
  }
  return "_UNKNOWN_";
}

void AuthenticatorImpl::verify(Http::HeaderMap& headers, Tracing::Span& parent_span,
                               std::vector<JwtLocationConstPtr>&& tokens,
                               SetPayloadCallback set_payload_cb, AuthenticatorCallback callback) {
  ASSERT(!callback_);
  headers_ = &headers;
  parent_span_ = &parent_span;
  tokens_ = std::move(tokens);
  set_payload_cb_ = std::move(set_payload_cb);
  callback_ = std::move(callback);

  ENVOY_LOG(debug, "{}: JWT authentication starts (allow_failed={}), tokens size={}", name(),
            is_allow_failed_, tokens_.size());
  if (tokens_.empty()) {
    doneWithStatus(Status::JwtMissed);
    return;
  }

  startVerify();
}

void AuthenticatorImpl::startVerify() {
  ASSERT(!tokens_.empty());
  ENVOY_LOG(debug, "{}: startVerify: tokens size {}", name(), tokens_.size());
  curr_token_ = std::move(tokens_.back());
  tokens_.pop_back();

  jwt_ = std::make_unique<::google::jwt_verify::Jwt>();
  const Status status = jwt_->parseFromString(curr_token_->token());
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  ENVOY_LOG(debug, "{}: Verifying JWT token of issuer {}", name(), jwt_->iss_);
  // Check if token extracted from the location contains the issuer specified by config.
  if (!curr_token_->isIssuerSpecified(jwt_->iss_)) {
    doneWithStatus(Status::JwtUnknownIssuer);
    return;
  }

  // TODO(qiwzhang): Cross-platform-wise the below unix_timestamp code is wrong as the
  // epoch is not guaranteed to be defined as the unix epoch. We should use
  // the abseil time functionality instead or use the jwt_verify_lib to check
  // the validity of a JWT.
  // Check "exp" claim.
  const uint64_t unix_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(timeSource().systemTime().time_since_epoch())
          .count();
  // If the nbf claim does *not* appear in the JWT, then the nbf field is defaulted
  // to 0.
  if (jwt_->nbf_ > unix_timestamp) {
    doneWithStatus(Status::JwtNotYetValid);
    return;
  }
  // If the exp claim does *not* appear in the JWT then the exp field is defaulted
  // to 0.
  if (jwt_->exp_ > 0 && jwt_->exp_ < unix_timestamp) {
    doneWithStatus(Status::JwtExpired);
    return;
  }

  // Check the issuer is configured or not.
  jwks_data_ = provider_ ? jwks_cache_.findByProvider(provider_.value())
                         : jwks_cache_.findByIssuer(jwt_->iss_);
  // isIssuerSpecified() check already make sure the issuer is in the cache.
  ASSERT(jwks_data_ != nullptr);

  // Check if audience is allowed
  bool is_allowed = check_audience_ ? check_audience_->areAudiencesAllowed(jwt_->audiences_)
                                    : jwks_data_->areAudiencesAllowed(jwt_->audiences_);
  if (!is_allowed) {
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

  // TODO(potatop): potential optimization.
  // Only one remote jwks will be fetched, verify will not continue util it is completed. This is
  // fine for provider name requirements, as each provider has only one issuer, but for allow
  // missing or failed there can be more than one issuers. This can be optimized; the same remote
  // jwks fetching can be shared by two requests.
  if (jwks_data_->getJwtProvider().has_remote_jwks()) {
    if (!fetcher_) {
      fetcher_ = create_jwks_fetcher_cb_(cm_);
    }
    fetcher_->fetch(jwks_data_->getJwtProvider().remote_jwks().http_uri(), *parent_span_, *this);
    return;
  }
  // No valid keys for this issuer. This may happen as a result of incorrect local
  // JWKS configuration.
  doneWithStatus(Status::JwksNoValidKeys);
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
  const Status status = ::google::jwt_verify::verifyJwt(*jwt_, *jwks_data_->getJwksObj());
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  // Forward the payload
  const auto& provider = jwks_data_->getJwtProvider();
  if (!provider.forward_payload_header().empty()) {
    headers_->addCopy(Http::LowerCaseString(provider.forward_payload_header()),
                      jwt_->payload_str_base64url_);
  }

  if (!provider.forward()) {
    // TODO(potatop) remove JWT from queries.
    // Remove JWT from headers.
    curr_token_->removeJwt(*headers_);
  }
  if (set_payload_cb_ && !provider.payload_in_metadata().empty()) {
    set_payload_cb_(provider.payload_in_metadata(), jwt_->payload_pb_);
  }

  doneWithStatus(Status::Ok);
}

void AuthenticatorImpl::doneWithStatus(const Status& status) {
  ENVOY_LOG(debug, "{}: JWT token verification completed with: {}", name(),
            ::google::jwt_verify::getStatusString(status));

  // If a request has multiple tokens, all of them must be valid. Otherwise it may have
  // following security hole: a request has a good token and a bad one, it will pass
  // verification, forwarded to the backend, and the backend may mistakenly use the bad
  // token as the good one that passed the verification.

  // Unless allowing failed or missing, all tokens must be verified successfully.
  if ((Status::Ok != status && !is_allow_failed_ && !is_allow_missing_) || tokens_.empty()) {
    tokens_.clear();
    if (is_allow_failed_) {
      callback_(Status::Ok);
    } else if (is_allow_missing_ && status == Status::JwtMissed) {
      callback_(Status::Ok);
    } else {
      callback_(status);
    }

    callback_ = nullptr;
    return;
  }
  startVerify();
}

} // namespace

AuthenticatorPtr Authenticator::create(const CheckAudience* check_audience,
                                       const absl::optional<std::string>& provider,
                                       bool allow_failed, bool allow_missing, JwksCache& jwks_cache,
                                       Upstream::ClusterManager& cluster_manager,
                                       CreateJwksFetcherCb create_jwks_fetcher_cb,
                                       TimeSource& time_source) {
  return std::make_unique<AuthenticatorImpl>(check_audience, provider, allow_failed, allow_missing,
                                             jwks_cache, cluster_manager, create_jwks_fetcher_cb,
                                             time_source);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
