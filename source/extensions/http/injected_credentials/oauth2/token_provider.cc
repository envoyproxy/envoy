#include "source/extensions/http/injected_credentials/oauth2/token_provider.h"
#include "token_provider.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

// TokenProvider Contructor
TokenProvider::TokenProvider(Common::SecretReaderConstSharedPtr secret_reader,
                             ThreadLocal::SlotAllocator& tls, Upstream::ClusterManager& cm,
                             const OAuth2& proto_config, Event::Dispatcher& dispatcher,
                             const std::string& stats_prefix, Stats::Scope& scope)
    : secret_reader_(secret_reader), tls_(tls.allocateSlot()),
      client_id_(proto_config.client_credentials().client_id()), dispatcher_(&dispatcher),
      stats_(generateStats(stats_prefix + "oauth2.", scope)) {
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
  if (timer_) {
    timer_->disableTimer();
    timer_.reset();
  }
  if (secret_reader_->credential().empty()) {
    ENVOY_LOG(error, "asyncGetAccessToken: client secret is empty, retrying in 2 seconds.");
    timer_ = dispatcher_->createTimer([this]() -> void { asyncGetAccessToken(); });
    timer_->enableTimer(std::chrono::seconds(2));
    stats_.token_fetch_failed_on_client_secret_.inc();
    return;
  }
  auto result = oauth2_client_->asyncGetAccessToken(client_id_, secret_reader_->credential());
  if (result == OAuth2Client::GetTokenResult::NotDispatchedClusterNotFound) {
    ENVOY_LOG(error, "asyncGetAccessToken: OAuth cluster not found., retrying in 2 seconds.");
    timer_ = dispatcher_->createTimer([this]() -> void { asyncGetAccessToken(); });
    timer_->enableTimer(std::chrono::seconds(2));
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
  if (!timer_) {
    timer_ = dispatcher_->createTimer([this]() -> void { asyncGetAccessToken(); });
  }
  timer_->enableTimer(expires_in / 2);
  stats_.token_fetched_.inc();
}

void TokenProvider::onGetAccessTokenFailure() {
  ENVOY_LOG(error, "onGetAccessTokenFailure: Failed to get access token");
  stats_.token_fetch_failed_on_oauth_server_response_.inc();
  asyncGetAccessToken();
}

const std::string& TokenProvider::token() const { return threadLocal().token(); }

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
