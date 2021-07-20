#include "source/extensions/filters/http/common/jwks_fetcher.h"

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
// Parameters of the jittered backoff strategy.
static constexpr uint32_t RetryInitialDelayMilliseconds = 1000;
static constexpr uint32_t RetryMaxDelayMilliseconds = 10 * 1000;

/**
 * @details construct an envoy.config.route.v3.RetryPolicy protobuf message
 *          from a less feature rich envoy.config.core.v3.RetryPolicy one.
 *
 *          this is about limiting the user's possibilities.
 *          just doing truncated exponential backoff
 *
 *          the upstream.use_retry feature flag will need to be turned on (default)
 *          for this to work.
 *
 * @param retry policy from the RemoteJwks proto
 * @return a retry policy usable by the http async client.
 */
envoy::config::route::v3::RetryPolicy
adaptRetryPolicy(const envoy::config::core::v3::RetryPolicy& core_retry_policy) {
  envoy::config::route::v3::RetryPolicy route_retry_policy;

  uint64_t base_interval_ms = RetryInitialDelayMilliseconds;
  uint64_t max_interval_ms = RetryMaxDelayMilliseconds;

  if (core_retry_policy.has_retry_back_off()) {
    const auto& core_back_off = core_retry_policy.retry_back_off();

    base_interval_ms = PROTOBUF_GET_MS_REQUIRED(core_back_off, base_interval);

    max_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(core_back_off, max_interval, base_interval_ms * 10);

    if (max_interval_ms < base_interval_ms) {
      throw EnvoyException("max_interval must be greater than or equal to the base_interval");
    }
  }

  route_retry_policy.mutable_num_retries()->set_value(
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(core_retry_policy, num_retries, 1));

  auto* route_mutable_back_off = route_retry_policy.mutable_retry_back_off();

  route_mutable_back_off->mutable_base_interval()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(base_interval_ms));
  route_mutable_back_off->mutable_max_interval()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(max_interval_ms));

  // set all the other fields with appropriate values.
  route_retry_policy.set_retry_on("5xx,gateway-error,connect-failure,reset");
  route_retry_policy.mutable_per_try_timeout()->CopyFrom(
      route_retry_policy.retry_back_off().max_interval());

  return route_retry_policy;
}

class JwksFetcherImpl : public JwksFetcher,
                        public Logger::Loggable<Logger::Id::filter>,
                        public Http::AsyncClient::Callbacks {
public:
  JwksFetcherImpl(Upstream::ClusterManager& cm, const RemoteJwks& remote_jwks)
      : cm_(cm), remote_jwks_(remote_jwks) {
    ENVOY_LOG(trace, "{}", __func__);

    if (remote_jwks_.has_retry_policy()) {
      route_retry_policy_ = adaptRetryPolicy(remote_jwks_.retry_policy());
    }
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
    ENVOY_LOG(debug, "fetch pubkey from [uri = {}]: start", remote_jwks_.http_uri().uri());
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(remote_jwks_.http_uri().timeout())))
                       .setParentSpan(parent_span)
                       .setChildSpanName("JWT Remote PubKey Fetch");

    if (remote_jwks_.has_retry_policy()) {
      options.setRetryPolicy(route_retry_policy_.value());
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

  // http async client uses richer semantics than the ones allowed in RemoteJwks
  // envoy.config.route.v3.RetryPolicy vs envoy.config.core.v3.RetryPolicy
  // mapping is done in constructor and reused.
  absl::optional<envoy::config::route::v3::RetryPolicy> route_retry_policy_{absl::nullopt};

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
