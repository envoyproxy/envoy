#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/context.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/server/filter_config.h"
#include "envoy/ssl/connection.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "common/common/empty_string.h"
#include "common/common/linked_object.h"
#include "common/http/message_impl.h"
#include "common/router/router.h"
#include "common/stream_info/stream_info_impl.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

class AsyncStreamImpl;
class AsyncRequestImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster, Stats::Store& stats_store,
                  Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info,
                  Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                  Random::RandomGenerator& random, Router::ShadowWriterPtr&& shadow_writer,
                  Http::Context& http_context);
  ~AsyncClientImpl() override;

  // Http::AsyncClient
  Request* send(RequestMessagePtr&& request, Callbacks& callbacks,
                const AsyncClient::RequestOptions& options) override;
  Stream* start(StreamCallbacks& callbacks, const AsyncClient::StreamOptions& options) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  Upstream::ClusterInfoConstSharedPtr cluster_;
  Router::FilterConfig config_;
  Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<AsyncStreamImpl>> active_streams_;

  friend class AsyncStreamImpl;
  friend class AsyncRequestImpl;
};

/**
 * Implementation of AsyncRequest. This implementation is capable of sending HTTP requests to a
 * ConnectionPool asynchronously.
 */
class AsyncStreamImpl : public AsyncClient::Stream,
                        public StreamDecoderFilterCallbacks,
                        public Event::DeferredDeletable,
                        Logger::Loggable<Logger::Id::http>,
                        public LinkedObject<AsyncStreamImpl>,
                        public ScopeTrackedObject {
public:
  AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                  const AsyncClient::StreamOptions& options);

  // Http::StreamDecoderFilterCallbacks
  void requestRouteConfigUpdate(Http::RouteConfigUpdatedCallbackSharedPtr) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  // Http::AsyncClient::Stream
  void sendHeaders(RequestHeaderMap& headers, bool end_stream) override;
  void sendData(Buffer::Instance& data, bool end_stream) override;
  void sendTrailers(RequestTrailerMap& trailers) override;
  void reset() override;
  bool isAboveWriteBufferHighWatermark() const override { return high_watermark_calls_ > 0; }

protected:
  bool remoteClosed() { return remote_closed_; }
  void closeLocal(bool end_stream);
  StreamInfo::StreamInfoImpl& streamInfo() override { return stream_info_; }

  AsyncClientImpl& parent_;

private:
  struct NullHedgePolicy : public Router::HedgePolicy {
    // Router::HedgePolicy
    uint32_t initialRequests() const override { return 1; }
    const envoy::type::v3::FractionalPercent& additionalRequestChance() const override {
      return additional_request_chance_;
    }
    bool hedgeOnPerTryTimeout() const override { return false; }

    const envoy::type::v3::FractionalPercent additional_request_chance_;
  };

  struct NullRateLimitPolicy : public Router::RateLimitPolicy {
    // Router::RateLimitPolicy
    const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
    getApplicableRateLimit(uint64_t) const override {
      return rate_limit_policy_entry_;
    }
    bool empty() const override { return true; }

    static const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
        rate_limit_policy_entry_;
  };

  struct NullRetryPolicy : public Router::RetryPolicy {
    // Router::RetryPolicy
    std::chrono::milliseconds perTryTimeout() const override {
      return std::chrono::milliseconds(0);
    }
    std::vector<Upstream::RetryHostPredicateSharedPtr> retryHostPredicates() const override {
      return {};
    }
    Upstream::RetryPrioritySharedPtr retryPriority() const override { return {}; }

    uint32_t hostSelectionMaxAttempts() const override { return 1; }
    uint32_t numRetries() const override { return 1; }
    uint32_t retryOn() const override { return 0; }
    const std::vector<uint32_t>& retriableStatusCodes() const override {
      return retriable_status_codes_;
    }
    const std::vector<Http::HeaderMatcherSharedPtr>& retriableHeaders() const override {
      return retriable_headers_;
    }
    const std::vector<Http::HeaderMatcherSharedPtr>& retriableRequestHeaders() const override {
      return retriable_request_headers_;
    }
    absl::optional<std::chrono::milliseconds> baseInterval() const override {
      return absl::nullopt;
    }
    absl::optional<std::chrono::milliseconds> maxInterval() const override { return absl::nullopt; }
    const std::vector<Router::ResetHeaderParserSharedPtr>& resetHeaders() const override {
      return reset_headers_;
    }
    std::chrono::milliseconds resetMaxInterval() const override {
      return std::chrono::milliseconds(300000);
    }

    const std::vector<uint32_t> retriable_status_codes_{};
    const std::vector<Http::HeaderMatcherSharedPtr> retriable_headers_{};
    const std::vector<Http::HeaderMatcherSharedPtr> retriable_request_headers_{};
    const std::vector<Router::ResetHeaderParserSharedPtr> reset_headers_{};
  };

  struct NullConfig : public Router::Config {
    Router::RouteConstSharedPtr route(const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                                      uint64_t) const override {
      return nullptr;
    }

    Router::RouteConstSharedPtr route(const Router::RouteCallback&, const Http::RequestHeaderMap&,
                                      const StreamInfo::StreamInfo&, uint64_t) const override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    const std::list<LowerCaseString>& internalOnlyHeaders() const override {
      return internal_only_headers_;
    }

    const std::string& name() const override { return EMPTY_STRING; }
    bool usesVhds() const override { return false; }
    bool mostSpecificHeaderMutationsWins() const override { return false; }

    static const std::list<LowerCaseString> internal_only_headers_;
  };

  struct NullVirtualHost : public Router::VirtualHost {
    // Router::VirtualHost
    Stats::StatName statName() const override { return {}; }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    const Router::Config& routeConfig() const override { return route_configuration_; }
    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }
    bool includeAttemptCountInRequest() const override { return false; }
    bool includeAttemptCountInResponse() const override { return false; }
    uint32_t retryShadowBufferLimit() const override {
      return std::numeric_limits<uint32_t>::max();
    }
    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullConfig route_configuration_;
  };

  struct NullPathMatchCriterion : public Router::PathMatchCriterion {
    Router::PathMatchType matchType() const override { return Router::PathMatchType::None; }
    const std::string& matcher() const override { return EMPTY_STRING; }
  };

  struct RouteEntryImpl : public Router::RouteEntry {
    RouteEntryImpl(
        const std::string& cluster_name, const absl::optional<std::chrono::milliseconds>& timeout,
        const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
            hash_policy)
        : cluster_name_(cluster_name), timeout_(timeout) {
      if (!hash_policy.empty()) {
        hash_policy_ = std::make_unique<HashPolicyImpl>(hash_policy);
      }
    }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    Http::Code clusterNotFoundResponseCode() const override {
      return Http::Code::InternalServerError;
    }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    void finalizeRequestHeaders(Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                                bool) const override {}
    void finalizeResponseHeaders(Http::ResponseHeaderMap&,
                                 const StreamInfo::StreamInfo&) const override {}
    const HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
    const Router::HedgePolicy& hedgePolicy() const override { return hedge_policy_; }
    const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
    Upstream::ResourcePriority priority() const override {
      return Upstream::ResourcePriority::Default;
    }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::RetryPolicy& retryPolicy() const override { return retry_policy_; }
    const Router::InternalRedirectPolicy& internalRedirectPolicy() const override {
      return internal_redirect_policy_;
    }
    uint32_t retryShadowBufferLimit() const override {
      return std::numeric_limits<uint32_t>::max();
    }
    const std::vector<Router::ShadowPolicyPtr>& shadowPolicies() const override {
      return shadow_policies_;
    }
    std::chrono::milliseconds timeout() const override {
      if (timeout_) {
        return timeout_.value();
      } else {
        return std::chrono::milliseconds(0);
      }
    }
    absl::optional<std::chrono::milliseconds> idleTimeout() const override { return absl::nullopt; }
    absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
      return absl::nullopt;
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderMax() const override {
      return absl::nullopt;
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutHeaderOffset() const override {
      return absl::nullopt;
    }
    absl::optional<std::chrono::milliseconds> maxGrpcTimeout() const override {
      return absl::nullopt;
    }
    absl::optional<std::chrono::milliseconds> grpcTimeoutOffset() const override {
      return absl::nullopt;
    }
    const Router::VirtualCluster* virtualCluster(const Http::HeaderMap&) const override {
      return nullptr;
    }
    const Router::TlsContextMatchCriteria* tlsContextMatchCriteria() const override {
      return nullptr;
    }
    const std::multimap<std::string, std::string>& opaqueConfig() const override {
      return opaque_config_;
    }
    const Router::VirtualHost& virtualHost() const override { return virtual_host_; }
    bool autoHostRewrite() const override { return false; }
    bool includeVirtualHostRateLimits() const override { return true; }
    const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
    const Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }
    const Router::PathMatchCriterion& pathMatchCriterion() const override {
      return path_match_criterion_;
    }

    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }
    const absl::optional<ConnectConfig>& connectConfig() const override {
      return connect_config_nullopt_;
    }

    bool includeAttemptCountInRequest() const override { return false; }
    bool includeAttemptCountInResponse() const override { return false; }
    const Router::RouteEntry::UpgradeMap& upgradeMap() const override { return upgrade_map_; }
    const std::string& routeName() const override { return route_name_; }
    std::unique_ptr<const HashPolicyImpl> hash_policy_;
    static const NullHedgePolicy hedge_policy_;
    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullRetryPolicy retry_policy_;
    static const Router::InternalRedirectPolicyImpl internal_redirect_policy_;
    static const std::vector<Router::ShadowPolicyPtr> shadow_policies_;
    static const NullVirtualHost virtual_host_;
    static const std::multimap<std::string, std::string> opaque_config_;
    static const envoy::config::core::v3::Metadata metadata_;
    // Async client doesn't require metadata.
    static const Config::TypedMetadataImpl<Config::TypedMetadataFactory> typed_metadata_;
    static const NullPathMatchCriterion path_match_criterion_;

    Router::RouteEntry::UpgradeMap upgrade_map_;
    const std::string& cluster_name_;
    absl::optional<std::chrono::milliseconds> timeout_;
    static const absl::optional<ConnectConfig> connect_config_nullopt_;
    const std::string route_name_;
  };

  struct RouteImpl : public Router::Route {
    RouteImpl(const std::string& cluster_name,
              const absl::optional<std::chrono::milliseconds>& timeout,
              const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
                  hash_policy)
        : route_entry_(cluster_name, timeout, hash_policy) {}

    // Router::Route
    const Router::DirectResponseEntry* directResponseEntry() const override { return nullptr; }
    const Router::RouteEntry* routeEntry() const override { return &route_entry_; }
    const Router::Decorator* decorator() const override { return nullptr; }
    const Router::RouteTracing* tracingConfig() const override { return nullptr; }
    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }

    RouteEntryImpl route_entry_;
  };

  void cleanup();
  void closeRemote(bool end_stream);
  bool complete() { return local_closed_ && remote_closed_; }

  // Http::StreamDecoderFilterCallbacks
  const Network::Connection* connection() override { return nullptr; }
  Event::Dispatcher& dispatcher() override { return parent_.dispatcher_; }
  void resetStream() override;
  Router::RouteConstSharedPtr route() override { return route_; }
  Router::RouteConstSharedPtr route(const Router::RouteCallback&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override { return parent_.cluster_; }
  void clearRouteCache() override {}
  uint64_t streamId() const override { return stream_id_; }
  Tracing::Span& activeSpan() override { return active_span_; }
  const Tracing::Config& tracingConfig() override { return tracing_config_; }
  void continueDecoding() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  RequestTrailerMap& addDecodedTrailers() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void addDecodedData(Buffer::Instance&, bool) override {
    // This should only be called if the user has set up buffering. The request is already fully
    // buffered. Note that this is only called via the async client's internal use of the router
    // filter which uses this function for buffering.
    ASSERT(buffered_body_ != nullptr);
  }
  MetadataMapVector& addDecodedMetadata() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void injectDecodedDataToFilterChain(Buffer::Instance&, bool) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  const Buffer::Instance* decodingBuffer() override { return buffered_body_.get(); }
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void sendLocalReply(Code code, absl::string_view body,
                      std::function<void(ResponseHeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                      absl::string_view details) override {
    if (encoded_response_headers_) {
      resetStream();
      return;
    }
    Utility::sendLocalReply(
        remote_closed_,
        Utility::EncodeFunctions{nullptr, nullptr,
                                 [this, modify_headers, &details](ResponseHeaderMapPtr&& headers,
                                                                  bool end_stream) -> void {
                                   if (modify_headers != nullptr) {
                                     modify_headers(*headers);
                                   }
                                   encodeHeaders(std::move(headers), end_stream, details);
                                 },
                                 [this](Buffer::Instance& data, bool end_stream) -> void {
                                   encodeData(data, end_stream);
                                 }},
        Utility::LocalReplyData{is_grpc_request_, code, body, grpc_status, is_head_request_});
  }
  // The async client won't pause if sending an Expect: 100-Continue so simply
  // swallows any incoming encode100Continue.
  void encode100ContinueHeaders(ResponseHeaderMapPtr&&) override {}
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override;
  void encodeMetadata(MetadataMapPtr&&) override {}
  void onDecoderFilterAboveWriteBufferHighWatermark() override { ++high_watermark_calls_; }
  void onDecoderFilterBelowWriteBufferLowWatermark() override {
    ASSERT(high_watermark_calls_ != 0);
    --high_watermark_calls_;
  }
  void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void setDecoderBufferLimit(uint32_t) override {}
  uint32_t decoderBufferLimit() override { return 0; }
  bool recreateStream() override { return false; }
  const ScopeTrackedObject& scope() override { return *this; }
  void addUpstreamSocketOptions(const Network::Socket::OptionsSharedPtr&) override {}
  Network::Socket::OptionsSharedPtr getUpstreamSocketOptions() const override { return {}; }

  // ScopeTrackedObject
  void dumpState(std::ostream& os, int indent_level) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "AsyncClient " << this << DUMP_MEMBER(stream_id_) << "\n";
    DUMP_DETAILS(&stream_info_);
  }

  AsyncClient::StreamCallbacks& stream_callbacks_;
  const uint64_t stream_id_;
  Router::ProdFilter router_;
  StreamInfo::StreamInfoImpl stream_info_;
  Tracing::NullSpan active_span_;
  const Tracing::Config& tracing_config_;
  std::shared_ptr<RouteImpl> route_;
  uint32_t high_watermark_calls_{};
  bool local_closed_{};
  bool remote_closed_{};
  Buffer::InstancePtr buffered_body_;
  bool encoded_response_headers_{};
  bool is_grpc_request_{};
  bool is_head_request_{false};
  bool send_xff_{true};

  friend class AsyncClientImpl;
  friend class AsyncClientImplUnitTest;
};

class AsyncRequestImpl final : public AsyncClient::Request,
                               AsyncStreamImpl,
                               AsyncClient::StreamCallbacks {
public:
  AsyncRequestImpl(RequestMessagePtr&& request, AsyncClientImpl& parent,
                   AsyncClient::Callbacks& callbacks, const AsyncClient::RequestOptions& options);

  // AsyncClient::Request
  void cancel() override;

private:
  void initialize();

  // AsyncClient::StreamCallbacks
  void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(ResponseTrailerMapPtr&& trailers) override;
  void onComplete() override;
  void onReset() override;

  // Http::StreamDecoderFilterCallbacks
  void addDecodedData(Buffer::Instance&, bool) override {
    // The request is already fully buffered. Note that this is only called via the async client's
    // internal use of the router filter which uses this function for buffering.
  }
  const Buffer::Instance* decodingBuffer() override { return &request_->body(); }
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  RequestMessagePtr request_;
  AsyncClient::Callbacks& callbacks_;
  std::unique_ptr<ResponseMessageImpl> response_;
  bool cancelled_{};
  Tracing::SpanPtr child_span_;

  friend class AsyncClientImpl;
};

} // namespace Http
} // namespace Envoy
