#include "extensions/filters/http/common/jwks_fetcher.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "jwt_verify_lib/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

class JwksFetcherImpl : public JwksFetcher,
                        public Logger::Loggable<Logger::Id::filter>,
                        public Http::AsyncClient::Callbacks {
public:
  JwksFetcherImpl(Upstream::ClusterManager& cm) : cm_(cm) { ENVOY_LOG(trace, "{}", __func__); }

  ~JwksFetcherImpl() { cancel(); }

  void cancel() {
    if (request_ && !complete_) {
      request_->cancel();
      ENVOY_LOG(debug, "fetch pubkey [uri = {}]: canceled", uri_->uri());
    }
    reset();
  }

  void fetch(const ::envoy::api::v2::core::HttpUri& uri, JwksFetcher::JwksReceiver& receiver) {
    ENVOY_LOG(trace, "{}", __func__);
    ASSERT(!receiver_);
    complete_ = false;
    receiver_ = &receiver;
    uri_ = &uri;
    Http::MessagePtr message = Http::Utility::prepareHeaders(uri);
    message->headers().insertMethod().value().setReference(Http::Headers::get().MethodValues.Get);
    ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", uri_->uri());
    request_ = cm_.httpAsyncClientForCluster(uri.cluster())
                   .send(std::move(message), *this,
                         Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(
                             DurationUtil::durationToMilliseconds(uri.timeout()))));
  }

  // HTTP async receive methods
  void onSuccess(Http::MessagePtr&& response) {
    ENVOY_LOG(trace, "{}", __func__);
    complete_ = true;
    const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
    if (status_code == enumToInt(Http::Code::OK)) {
      ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: success", __func__, uri_->uri());
      if (response->body()) {
        const auto len = response->body()->length();
        const auto body = std::string(static_cast<char*>(response->body()->linearize(len)), len);
        auto jwks =
            google::jwt_verify::Jwks::createFrom(body, google::jwt_verify::Jwks::Type::JWKS);
        if (jwks->getStatus() == google::jwt_verify::Status::Ok) {
          ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: succeeded", __func__, uri_->uri());
          receiver_->onJwksSuccess(std::move(jwks));
        } else {
          ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: invalid jwks", __func__, uri_->uri());
          receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
        }
      } else {
        ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: body is empty", __func__, uri_->uri());
        receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
      }
    } else {
      ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: response status code {}", __func__,
                uri_->uri(), status_code);
      receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
    }
    reset();
  }

  void onFailure(Http::AsyncClient::FailureReason reason) {
    ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: network error {}", __func__, uri_->uri(),
              enumToInt(reason));
    complete_ = true;
    receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
    reset();
  }

private:
  Upstream::ClusterManager& cm_;
  bool complete_{};
  JwksFetcher::JwksReceiver* receiver_{};
  const envoy::api::v2::core::HttpUri* uri_{};
  Http::AsyncClient::Request* request_{};

  void reset() {
    request_ = nullptr;
    receiver_ = nullptr;
    uri_ = nullptr;
  }
};
} // namespace

JwksFetcherPtr JwksFetcher::create(Upstream::ClusterManager& cm) {
  return std::make_unique<JwksFetcherImpl>(cm);
}
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
