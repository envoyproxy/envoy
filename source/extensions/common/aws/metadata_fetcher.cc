#include "source/extensions/common/aws/metadata_fetcher.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

namespace {

class MetadataFetcherImpl : public MetadataFetcher,
                            public Logger::Loggable<Logger::Id::aws>,
                            public Http::AsyncClient::Callbacks {

public:
  MetadataFetcherImpl(Upstream::ClusterManager& cm, absl::string_view cluster_name)
      : cm_(cm), cluster_name_(std::string(cluster_name)) {}

  // TODO(suniltheta): Verify that bypassing virtual dispatch here was intentional
  ~MetadataFetcherImpl() override { MetadataFetcherImpl::cancel(); }

  void cancel() override {
    if (request_ && !complete_) {
      request_->cancel();
      ENVOY_LOG(debug, "fetch AWS Metadata [cluster = {}]: cancelled", cluster_name_);
    }
    reset();
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
    complete_ = false;
    receiver_ = makeOptRef(receiver);

    // Stop processing if we are shutting down
    if (cm_.isShutdown()) {
      return;
    }

    const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster_name_);
    if (thread_local_cluster == nullptr) {
      ENVOY_LOG(error, "{} AWS Metadata failed: [cluster = {}] not found", __func__, cluster_name_);
      complete_ = true;
      receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::MissingConfig);
      reset();
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
    request_ = makeOptRefFromPtr(
        thread_local_cluster->httpAsyncClient().send(std::move(messagePtr), *this, options));
  }

  // HTTP async receive method on success.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override {
    ASSERT(receiver_);
    complete_ = true;
    const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
    if (status_code == enumToInt(Http::Code::OK)) {
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
    reset();
  }

  // HTTP async receive method on failure.
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override {
    ASSERT(receiver_);
    ENVOY_LOG(debug, "{}: fetch AWS Metadata [cluster = {}]: network error {}", __func__,
              cluster_name_, enumToInt(reason));
    complete_ = true;
    receiver_->onMetadataError(MetadataFetcher::MetadataReceiver::Failure::Network);
    reset();
  }

  // TODO(suniltheta): Add metadata fetch status into the span like it is done on ext_authz filter.
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  bool complete_{};
  Upstream::ClusterManager& cm_;
  const std::string cluster_name_;
  OptRef<MetadataFetcher::MetadataReceiver> receiver_;
  OptRef<Http::AsyncClient::Request> request_;

  void reset() { request_.reset(); }
};
} // namespace

MetadataFetcherPtr MetadataFetcher::create(Upstream::ClusterManager& cm,
                                           absl::string_view cluster_name) {
  return std::make_unique<MetadataFetcherImpl>(cm, cluster_name);
}
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
