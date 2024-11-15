#include "source/extensions/filters/http/common/jwks_fetcher.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "jwt_verify_lib/status.h"

using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

class JwksFetcherImpl : public JwksFetcher,
                        public Logger::Loggable<Logger::Id::filter>,
                        public Http::AsyncClient::Callbacks {
public:
  JwksFetcherImpl(Upstream::ClusterManager& cm, const RemoteJwks& remote_jwks)
      : cm_(cm), remote_jwks_(remote_jwks) {
    ENVOY_LOG(trace, "{}", __func__);
  }

  ~JwksFetcherImpl() override { cancel(); }

  void cancel() final {
    if (request_ && !complete_) {
      request_->cancel();
      ENVOY_LOG(debug, "fetch pubkey [uri = {}]: canceled", remote_jwks_.http_uri().uri());
    }
    reset();
  }

  void fetch(Tracing::Span& parent_span, JwksFetcher::JwksReceiver& receiver) override {
    ENVOY_LOG(trace, "{}", __func__);
    ASSERT(!receiver_);

    complete_ = false;
    receiver_ = &receiver;

    // Check if cluster is configured, fail the request if not.
    const auto thread_local_cluster = cm_.getThreadLocalCluster(remote_jwks_.http_uri().cluster());
    if (thread_local_cluster == nullptr) {
      ENVOY_LOG(error, "{}: fetch pubkey [uri = {}] failed: [cluster = {}] is not configured",
                __func__, remote_jwks_.http_uri().uri(), remote_jwks_.http_uri().cluster());
      complete_ = true;
      receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
      reset();
      return;
    }

    Http::RequestMessagePtr message = Http::Utility::prepareHeaders(remote_jwks_.http_uri());
    message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
    message->headers().setReferenceUserAgent(Http::Headers::get().UserAgentValues.GoBrowser);
    ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", remote_jwks_.http_uri().uri());
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(remote_jwks_.http_uri().timeout())))
                       .setParentSpan(parent_span)
                       .setChildSpanName("JWT Remote PubKey Fetch");

    if (remote_jwks_.has_retry_policy()) {
      envoy::config::route::v3::RetryPolicy route_retry_policy =
          Http::Utility::convertCoreToRouteRetryPolicy(remote_jwks_.retry_policy(),
                                                       "5xx,gateway-error,connect-failure,reset");
      options.setRetryPolicy(route_retry_policy);
      options.setBufferBodyForRetry(true);
    }

    request_ = thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);
  }

  // HTTP async receive methods
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override {
    ENVOY_LOG(trace, "{}", __func__);
    complete_ = true;
    const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
    const std::string& uri = remote_jwks_.http_uri().uri();
    if (status_code == enumToInt(Http::Code::OK)) {
      ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: success", __func__, uri);
      if (response->body().length() != 0) {
        const auto body = response->bodyAsString();
        auto jwks =
            google::jwt_verify::Jwks::createFrom(body, google::jwt_verify::Jwks::Type::JWKS);
        if (jwks->getStatus() == google::jwt_verify::Status::Ok) {
          ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: succeeded", __func__, uri);
          receiver_->onJwksSuccess(std::move(jwks));
        } else {
          ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: invalid jwks", __func__, uri);
          receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
        }
      } else {
        ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: body is empty", __func__, uri);
        receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
      }
    } else {
      ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: response status code {}", __func__, uri,
                status_code);
      receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
    }
    reset();
  }

  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override {
    ENVOY_LOG(debug, "{}: fetch pubkey [uri = {}]: network error {}", __func__,
              remote_jwks_.http_uri().uri(), enumToInt(reason));
    complete_ = true;
    receiver_->onJwksError(JwksFetcher::JwksReceiver::Failure::Network);
    reset();
  }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ClusterManager& cm_;
  bool complete_{};
  JwksFetcher::JwksReceiver* receiver_{};
  const RemoteJwks& remote_jwks_;
  Http::AsyncClient::Request* request_{};

  void reset() {
    request_ = nullptr;
    receiver_ = nullptr;
  }
};
} // namespace

JwksFetcherPtr JwksFetcher::create(
    Upstream::ClusterManager& cm,
    const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks) {
  return std::make_unique<JwksFetcherImpl>(cm, remote_jwks);
}
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
