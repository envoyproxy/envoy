#include "source/extensions/http/injected_credentials/oauth2/token_provider.h"
#include "token_provider.h"

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

namespace {

constexpr absl::string_view DEFAULT_AUTH_SCOPE = "";
// Transforms the proto list of 'auth_scopes' into a vector of std::string, also
// handling the default value logic.
std::string oauthScopesList(const Protobuf::RepeatedPtrField<std::string>& auth_scopes_protos) {
  std::vector<std::string> scopes;

  // If 'auth_scopes' is empty it must return a list with the default value.
  if (auth_scopes_protos.empty()) {
    scopes.emplace_back(DEFAULT_AUTH_SCOPE);
  } else {
    scopes.reserve(auth_scopes_protos.size());

    for (const auto& scope : auth_scopes_protos) {
      scopes.emplace_back(scope);
    }
  }
  return absl::StrJoin(scopes, " ");
}
} // namespace

// TokenProvider Constructor
TokenProvider::TokenProvider(Common::SecretReaderConstSharedPtr secret_reader,
                             ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
                             const OAuth2& proto_config, Event::Dispatcher& dispatcher,
                             const std::string& stats_prefix, Stats::Scope& scope)
    : secret_reader_(secret_reader), tls_(tls.allocateSlot()),
      client_id_(proto_config.client_credentials().client_id()),
      oauth_scopes_(oauthScopesList(proto_config.scopes())), dispatcher_(&dispatcher),
      stats_(generateStats(stats_prefix + "oauth2.", scope)),
      retry_interval_(
          proto_config.token_fetch_retry_interval().seconds() > 0
              ? std::chrono::seconds(proto_config.token_fetch_retry_interval().seconds())
              : std::chrono::seconds(2)) {
  timer_ = dispatcher_->createTimer([this]() -> void { asyncGetAccessToken(); });
  ThreadLocalOauth2ClientCredentialsTokenSharedPtr empty(
      new ThreadLocalOauth2ClientCredentialsToken(""));
  tls_->set(
      [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });
  // initialize oauth2 client
  oauth2_client_ = std::make_unique<OAuth2ClientImpl>(cm, proto_config.token_endpoint());
  // set the callback for the oauth2 client
  oauth2_client_->setCallbacks(*this);
  asyncGetAccessToken();
}

// TokenProvider asyncGetAccessToken
void TokenProvider::asyncGetAccessToken() {
  // get the access token from the oauth2 client
  if (timer_->enabled()) {
    timer_->disableTimer();
  }
  if (secret_reader_->credential().empty()) {
    ENVOY_LOG(error, "asyncGetAccessToken: client secret is empty, retrying in {} seconds.",
              retry_interval_.count());
    timer_->enableTimer(std::chrono::seconds(retry_interval_));
    stats_.token_fetch_failed_on_client_secret_.inc();
    return;
  }
  auto result =
      oauth2_client_->asyncGetAccessToken(client_id_, secret_reader_->credential(), oauth_scopes_);
  if (result == OAuth2Client::GetTokenResult::NotDispatchedAlreadyInFlight) {
    return;
  }
  if (result == OAuth2Client::GetTokenResult::NotDispatchedClusterNotFound) {
    ENVOY_LOG(error, "asyncGetAccessToken: OAuth cluster not found. Retrying in {} seconds.",
              retry_interval_.count());
    timer_->enableTimer(std::chrono::seconds(retry_interval_));
    stats_.token_fetch_failed_on_cluster_not_found_.inc();
    return;
  }

  stats_.token_requested_.inc();
  ENVOY_LOG(debug, "asyncGetAccessToken: Dispatched OAuth request for access token.");
}

// FilterCallbacks
void TokenProvider::onGetAccessTokenSuccess(const std::string& access_token,
                                            std::chrono::seconds expires_in) {
  // set the token
  auto token = absl::StrCat("Bearer ", access_token);
  ThreadLocalOauth2ClientCredentialsTokenSharedPtr value(
      new ThreadLocalOauth2ClientCredentialsToken(token));

  tls_->set(
      [value](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return value; });

  stats_.token_fetched_.inc();
  ENVOY_LOG(debug, "onGetAccessTokenSuccess: Token fetched successfully, expires in {} seconds.",
            expires_in.count());
  if (timer_->enabled()) {
    return;
  }

  timer_->enableTimer(expires_in / 2);
}

void TokenProvider::onGetAccessTokenFailure(FailureReason failure_reason) {
  ENVOY_LOG(error, "onGetAccessTokenFailure: Failed to get access token");
  bool retry = true;
  switch (failure_reason) {
  case FailureReason::StreamReset:
    stats_.token_fetch_failed_on_stream_reset_.inc();
    break;
  case FailureReason::BadToken:
    stats_.token_fetch_failed_on_bad_token_.inc();
    retry = false;
    break;
  case FailureReason::BadResponseCode:
    stats_.token_fetch_failed_on_bad_response_code_.inc();
    break;
  }
  if (!retry) {
    return;
  }

  if (timer_->enabled()) {
    return;
  }
  timer_->enableTimer(retry_interval_);
}

const std::string& TokenProvider::credential() const { return threadLocal().token(); }

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
