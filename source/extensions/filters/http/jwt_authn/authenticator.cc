#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "envoy/http/async_client.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Object to implement Authenticator interface.
 */
class AuthenticatorImpl : public Logger::Loggable<Logger::Id::filter>,
                          public Authenticator,
                          public Http::AsyncClient::Callbacks {
public:
  AuthenticatorImpl(const AudienceCheckerSupplier& audience_checker_suppiler,
                    const absl::optional<std::string>& provider, JwksCache& jwks_cache,
                    Upstream::ClusterManager& cluster_manager)
      : jwks_cache_(jwks_cache), cm_(cluster_manager),
        audience_checker_suppiler_(audience_checker_suppiler), provider_(provider) {}

  // Following functions are for Authenticator interface
  void verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
              AuthenticatorCallback callback) override;
  void onDestroy() override;

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

  // Start verification process. It will continue to eliminate tokens with invalid claims until it
  // finds one to verify with key.
  void startVerify();

  // The jwks cache object.
  JwksCache& jwks_cache_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;

  // The token data
  std::vector<JwtLocationConstPtr> tokens_;
  JwtLocationConstPtr curr_token_;
  // The JWT object.
  ::google::jwt_verify::Jwt jwt_;
  // The JWKS data object
  JwksCache::JwksData* jwks_data_{};

  // The HTTP request headers
  Http::HeaderMap* headers_{};
  // The on_done function.
  AuthenticatorCallback callback_;

  // The pending uri_, only used for logging.
  std::string uri_;
  // The pending remote request so it can be canceled.
  Http::AsyncClient::Request* request_{};
  // audience checker suppiler.
  const AudienceCheckerSupplier& audience_checker_suppiler_;
  // specific provider or not when it is allow missing or failed.
  const absl::optional<std::string> provider_;
};

void AuthenticatorImpl::verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
                               AuthenticatorCallback callback) {
  headers_ = &headers;
  tokens_ = std::move(tokens);
  callback_ = std::move(callback);

  ENVOY_LOG(debug, "Jwt authentication starts");
  if (tokens_.empty()) {
    doneWithStatus(Status::JwtMissed);
    return;
  }

  startVerify();
}

void AuthenticatorImpl::startVerify() {
  Status status = Status::JwtVerificationFail;
  while (!tokens_.empty()) {
    curr_token_ = std::move(tokens_.back());
    tokens_.pop_back();
    jwt_ = {};
    status = jwt_.parseFromString(curr_token_->token());
    if (status != Status::Ok) {
      continue;
    }

    // Check if token extracted from the location contains the issuer specified by config.
    if (!curr_token_->isIssuerSpecified(jwt_.iss_)) {
      ENVOY_LOG(debug, "Jwt issuer {} does not match required", jwt_.iss_);
      status = Status::JwtUnknownIssuer;
      continue;
    }

    // Check "exp" claim.
    const auto unix_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    // NOTE: Service account tokens generally don't have an expiration time (due to being long
    // lived) and defaulted to 0 by google::jwt_verify library but are still valid.
    if (jwt_.exp_ > 0 && jwt_.exp_ < unix_timestamp) {
      status = Status::JwtExpired;
      continue;
    }

    // Check the issuer is configured or not.
    if (provider_) {
      jwks_data_ = jwks_cache_.findByProvider(provider_.value());
    } else {
      jwks_data_ = jwks_cache_.findByIssuer(jwt_.iss_);
    }
    // isIssuerSpecified() check already make sure the issuer is in the cache.
    ASSERT(jwks_data_ != nullptr);

    const AudienceChecker& audience_checker =
        provider_ ? audience_checker_suppiler_.getAudienceCheckerByProvider(provider_.value())
                  : audience_checker_suppiler_.getAudienceCheckerByIssuer(jwt_.iss_);

    // Check if audience is allowed
    if (!audience_checker.areAudiencesAllowed(jwt_.audiences_)) {
      status = Status::JwtAudienceNotAllowed;
      continue;
    }

    if (jwks_data_->getJwksObj() != nullptr && !jwks_data_->isExpired()) {
      verifyKey();
      return;
    }

    // TODO(potatop): potential optimization.
    // Only one remote jwks will be fetched, verify will not continue util it is completed. This is
    // fine for provider name requirements, as each provider has only one issuer, but for allow
    // missing or failed there can be more than one issuers. This can be optimized; the same remote
    // jwks fetching can be shared by two requrests.
    fetchRemoteJwks();
    return;
  }
  // send the last error status
  doneWithStatus(status);
}

void AuthenticatorImpl::fetchRemoteJwks() {
  const auto& http_uri = jwks_data_->getJwtProvider().remote_jwks().http_uri();

  Http::MessagePtr message = Http::Utility::prepareHeaders(http_uri);
  message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);

  if (cm_.get(http_uri.cluster()) == nullptr) {
    doneWithStatus(Status::JwksFetchFail);
    return;
  }

  uri_ = http_uri.uri();
  ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", uri_);
  request_ = cm_.httpAsyncClientForCluster(http_uri.cluster())
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
    curr_token_->removeJwt(*headers_);
  }

  doneWithStatus(Status::Ok);
}

void AuthenticatorImpl::doneWithStatus(const Status& status) {
  // if on allow missing or failed this should verify all tokens, otherwise stop on ok.
  if ((Status::Ok == status && provider_) || tokens_.empty()) {
    tokens_.clear();
    ENVOY_LOG(debug, "Jwt authentication completed with: {}",
              ::google::jwt_verify::getStatusString(status));
    callback_(!provider_ ? Status::Ok : status);
    return;
  }
  startVerify();
}

} // namespace

AuthenticatorPtr Authenticator::create(const AudienceCheckerSupplier& audience_checker_suppiler,
                                       const absl::optional<std::string>& provider,
                                       JwksCache& jwks_cache,
                                       Upstream::ClusterManager& cluster_manager) {
  return std::make_unique<AuthenticatorImpl>(audience_checker_suppiler, provider, jwks_cache,
                                             cluster_manager);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
