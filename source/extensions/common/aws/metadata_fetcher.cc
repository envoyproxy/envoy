#include "source/extensions/common/aws/metadata_fetcher.h"

#include <memory>

#include "source/common/common/enum_to_int.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

namespace {

class MetadataFetcherImpl : public MetadataFetcher,
                            public Logger::Loggable<Logger::Id::aws>,
                            public Http::AsyncClient::Callbacks,
                            public std::enable_shared_from_this<MetadataFetcherImpl> {

public:
  MetadataFetcherImpl(Upstream::ClusterManager& cm, absl::string_view cluster_name)
      : cm_(cm), cluster_name_(std::string(cluster_name)) {}

  ~MetadataFetcherImpl() override {
    if (request_.has_value() && !complete_.load()) {
      if (!cm_.isShutdown()) {
        const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster_name_);
        if (thread_local_cluster != nullptr) {
          thread_local_cluster->httpAsyncClient().dispatcher().post(
              [self_ref = self_ref_]() { self_ref->cancel(); });
          return;
        }
      }
    }
    receiver_.reset();
    complete_.store(true);
    request_.reset();
    self_ref_.reset();
  }

  void cancel() override {
    if (request_.has_value() && !complete_.load()) {
      request_->cancel();
      ENVOY_LOG(debug, "fetch AWS Metadata [cluster = {}]: cancelled", cluster_name_);
    }

    receiver_.reset();
    complete_.store(true);
    request_.reset();
    self_ref_.reset();
  }

  absl::string_view failureToString(MetadataFetcher::MetadataReceiver::Failure reason) override {
    switch (reason) {
    case MetadataFetcher::MetadataReceiver::Failure::Network:
      return "Network";
    case MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata:
      return "InvalidMetadata";
    case MetadataFetcher::MetadataReceiver::Failure::MissingConfig:
      return "MissingConfig";
    default:
      return "";
    }
  }

  void fetch(Http::RequestMessage& message, Tracing::Span& parent_span,
             MetadataFetcher::MetadataReceiver& receiver) override {
    ASSERT(!request_);
    complete_.store(false);
    receiver_ = makeOptRef(receiver);

    // Stop processing if we are shutting down
    if (cm_.isShutdown()) {
      return;
    }

    const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster_name_);
    if (thread_local_cluster == nullptr) {
      ENVOY_LOG(error, "{} AWS Metadata failed: [cluster = {}] not found", __func__, cluster_name_);
      complete_.store(true);
      receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::MissingConfig);
      request_.reset();
      self_ref_.reset();
      return;
    }

    constexpr uint64_t MAX_RETRIES = 3;
    constexpr uint64_t RETRY_DELAY = 1000;
    constexpr uint64_t TIMEOUT = 5 * 1000;

    const auto host_attributes = Http::Utility::parseAuthority(message.headers().getHostValue());
    const auto host = host_attributes.host_;
    const auto path = message.headers().getPathValue();
    const auto scheme = message.headers().getSchemeValue();
    const auto method = message.headers().getMethodValue();

    const size_t query_offset = path.find('?');
    // Sanitize the path before logging.
    // However, the route debug log will still display the entire path.
    // So safely store the Envoy logs at debug level.
    const absl::string_view sanitized_path =
        query_offset != absl::string_view::npos ? path.substr(0, query_offset) : path;
    ENVOY_LOG(debug, "fetch AWS Metadata from the cluster {} at [uri = {}]", cluster_name_,
              fmt::format("{}://{}{}", scheme, host, sanitized_path));

    Http::RequestHeaderMapPtr headersPtr =
        Envoy::Http::createHeaderMap<Envoy::Http::RequestHeaderMapImpl>(
            {{Envoy::Http::Headers::get().Method, std::string(method)},
             {Envoy::Http::Headers::get().Host, std::string(host)},
             {Envoy::Http::Headers::get().Scheme, std::string(scheme)},
             {Envoy::Http::Headers::get().Path, std::string(path)}});

    // Copy the remaining headers.
    message.headers().iterate(
        [&headersPtr](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
          // Skip pseudo-headers
          if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
            return Http::HeaderMap::Iterate::Continue;
          }
          headersPtr->addCopy(Http::LowerCaseString(entry.key().getStringView()),
                              entry.value().getStringView());
          return Http::HeaderMap::Iterate::Continue;
        });

    auto messagePtr = std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headersPtr));
    // Add body if it exists, used when IAM Roles Anywhere exchanges X509 credentials for temporary
    // credentials
    if (message.body().length()) {
      messagePtr->body().add(message.body());
    }
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(std::chrono::milliseconds(TIMEOUT))
                       .setParentSpan(parent_span)
                       .setSendXff(false)
                       .setChildSpanName("AWS Metadata Fetch");

    envoy::config::route::v3::RetryPolicy route_retry_policy;
    route_retry_policy.mutable_num_retries()->set_value(MAX_RETRIES);
    route_retry_policy.mutable_per_try_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(TIMEOUT));
    route_retry_policy.mutable_per_try_idle_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(RETRY_DELAY));
    route_retry_policy.set_retry_on("5xx,gateway-error,connect-failure,reset,refused-stream");

    options.setRetryPolicy(route_retry_policy);
    options.setBufferBodyForRetry(true);
    // Keep object alive during async operation
    self_ref_ = shared_from_this();
    request_ = makeOptRefFromPtr(
        thread_local_cluster->httpAsyncClient().send(std::move(messagePtr), *this, options));

    if (!request_) {
      self_ref_.reset();
    }
  }

  // HTTP async receive method on success.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override {
    // Capture self-reference immediately to keep object alive during method execution
    auto self_ref = std::move(self_ref_);

    // Safe early exit if object is being destroyed
    if (!receiver_ || complete_.load()) {
      return;
    }
    complete_.store(true);
    const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
    if (status_code == enumToInt(Http::Code::OK) ||
        (status_code == enumToInt(Http::Code::Created))) {
      ENVOY_LOG(debug, "{}: fetch AWS Metadata [cluster = {}]: success", __func__, cluster_name_);
      if (response->body().length() != 0) {
        const auto body = response->bodyAsString();
        receiver_->onMetadataSuccess(std::move(body));
      } else {
        ENVOY_LOG(debug, "{}: fetch AWS Metadata [cluster = {}]: body is empty", __func__,
                  cluster_name_);
        receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::InvalidMetadata);
      }
    } else {
      if (response->body().length() != 0) {
        ENVOY_LOG(debug, "{}: fetch AWS Metadata [cluster = {}]: response status code {}, body: {}",
                  __func__, cluster_name_, status_code, response->bodyAsString());
      } else {
        ENVOY_LOG(debug,
                  "{}: fetch AWS Metadata [cluster = {}]: response status code {}, body is empty",
                  __func__, cluster_name_, status_code);
      }
      receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network);
    }
    request_.reset();
  }

  // HTTP async receive method on failure.
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override {
    // Capture self-reference immediately to keep object alive during method execution
    auto self_ref = std::move(self_ref_);

    // Safe early exit if object is being destroyed
    if (!receiver_ || complete_.load()) {
      return;
    }
    ENVOY_LOG(debug, "{}: fetch AWS Metadata [cluster = {}]: network error {}", __func__,
              cluster_name_, enumToInt(reason));
    complete_.store(true);
    receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network);
    request_.reset();
  }

  // TODO(suniltheta): Add metadata fetch status into the span like it is done on ext_authz filter.
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  std::atomic<bool> complete_{false};
  Upstream::ClusterManager& cm_;
  const std::string cluster_name_;
  OptRef<MetadataFetcher::MetadataReceiver> receiver_;
  OptRef<Http::AsyncClient::Request> request_;
  std::shared_ptr<MetadataFetcherImpl> self_ref_; // Keep self alive during async ops

  void reset() {
    request_.reset();
    self_ref_.reset();
  }
};
} // namespace

// TODO(nbaws): Change api to return shared_ptr and remove wrapper
class MetadataFetcherWrapper : public MetadataFetcher {
public:
  explicit MetadataFetcherWrapper(std::shared_ptr<MetadataFetcherImpl> impl)
      : impl_(std::move(impl)) {}

  void cancel() override { impl_->cancel(); }
  absl::string_view failureToString(MetadataReceiver::Failure reason) override {
    return impl_->failureToString(reason);
  }
  void fetch(Http::RequestMessage& message, Tracing::Span& parent_span,
             MetadataReceiver& receiver) override {
    impl_->fetch(message, parent_span, receiver);
  }

private:
  std::shared_ptr<MetadataFetcherImpl> impl_;
};

MetadataFetcherPtr MetadataFetcher::create(Upstream::ClusterManager& cm,
                                           absl::string_view cluster_name) {
  auto impl = std::make_shared<MetadataFetcherImpl>(cm, cluster_name);
  return std::make_unique<MetadataFetcherWrapper>(std::move(impl));
}
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
