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
#include "envoy/router/context.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/server/filter_config.h"
#include "envoy/ssl/connection.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/linked_object.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/common/upstream/retry_factory.h"
#include "source/extensions/early_data/default_early_data_policy.h"

namespace Envoy {
namespace Http {
namespace {
// Limit the size of buffer for data used for retries.
// This is currently fixed to 64KB.
constexpr uint64_t kBufferLimitForRetry = 1 << 16;
} // namespace

class AsyncStreamImpl;
class AsyncRequestImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(Upstream::ClusterInfoConstSharedPtr cluster, Stats::Store& stats_store,
                  Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info,
                  Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                  Random::RandomGenerator& random, Router::ShadowWriterPtr&& shadow_writer,
                  Http::Context& http_context, Router::Context& router_context);
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
  Singleton::Manager& singleton_manager_;

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
  ~AsyncStreamImpl() override { router_.onDestroy(); }

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

  struct NullConfig : public Router::Config {
    Router::RouteConstSharedPtr route(const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                                      uint64_t) const override {
      return nullptr;
    }

    Router::RouteConstSharedPtr route(const Router::RouteCallback&, const Http::RequestHeaderMap&,
                                      const StreamInfo::StreamInfo&, uint64_t) const override {
      return nullptr;
    }

    const std::list<LowerCaseString>& internalOnlyHeaders() const override {
      return internal_only_headers_;
    }

    const std::string& name() const override { return EMPTY_STRING; }
    bool usesVhds() const override { return false; }
    bool mostSpecificHeaderMutationsWins() const override { return false; }
    uint32_t maxDirectResponseBodySizeBytes() const override { return 0; }

    static const std::list<LowerCaseString> internal_only_headers_;
  };

  struct NullVirtualHost : public Router::VirtualHost {
    // Router::VirtualHost
    Stats::StatName statName() const override { return {}; }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    const Router::Config& routeConfig() const override { return route_configuration_; }
    bool includeAttemptCountInRequest() const override { return false; }
    bool includeAttemptCountInResponse() const override { return false; }
    uint32_t retryShadowBufferLimit() const override {
      return std::numeric_limits<uint32_t>::max();
    }
    const Router::RouteSpecificFilterConfig*
    mostSpecificPerFilterConfig(const std::string&) const override {
      return nullptr;
    }
    void traversePerFilterConfig(
        const std::string&,
        std::function<void(const Router::RouteSpecificFilterConfig&)>) const override {}
    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullConfig route_configuration_;
  };

  struct NullPathMatchCriterion : public Router::PathMatchCriterion {
    Router::PathMatchType matchType() const override { return Router::PathMatchType::None; }
    const std::string& matcher() const override { return EMPTY_STRING; }
  };

  struct RouteEntryImpl : public Router::RouteEntry {
    RouteEntryImpl(
        AsyncClientImpl& parent, const absl::optional<std::chrono::milliseconds>& timeout,
        const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
            hash_policy,
        const absl::optional<envoy::config::route::v3::RetryPolicy>& retry_policy)
        : cluster_name_(parent.cluster_->name()), timeout_(timeout) {
      if (!hash_policy.empty()) {
        hash_policy_ = std::make_unique<HashPolicyImpl>(hash_policy);
      }
      if (retry_policy.has_value()) {
        // ProtobufMessage::getStrictValidationVisitor() ?  how often do we do this?
        Upstream::RetryExtensionFactoryContextImpl factory_context(parent.singleton_manager_);
        retry_policy_ = std::make_unique<Router::RetryPolicyImpl>(
            retry_policy.value(), ProtobufMessage::getNullValidationVisitor(), factory_context);
      } else {
        retry_policy_ = std::make_unique<Router::RetryPolicyImpl>();
      }
    }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Router::RouteStatsContextOptRef routeStatsContext() const override {
      return Router::RouteStatsContextOptRef();
    }
    Http::Code clusterNotFoundResponseCode() const override {
      return Http::Code::InternalServerError;
    }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    absl::optional<std::string>
    currentUrlPathAfterRewrite(const Http::RequestHeaderMap&) const override {
      return absl::optional<std::string>();
    }
    void finalizeRequestHeaders(Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                                bool) const override {}
    Http::HeaderTransforms requestHeaderTransforms(const StreamInfo::StreamInfo&,
                                                   bool) const override {
      return {};
    }
    void finalizeResponseHeaders(Http::ResponseHeaderMap&,
                                 const StreamInfo::StreamInfo&) const override {}
    Http::HeaderTransforms responseHeaderTransforms(const StreamInfo::StreamInfo&,
                                                    bool) const override {
      return {};
    }
    const HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
    const Router::HedgePolicy& hedgePolicy() const override { return hedge_policy_; }
    const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
    Upstream::ResourcePriority priority() const override {
      return Upstream::ResourcePriority::Default;
    }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::RetryPolicy& retryPolicy() const override { return *retry_policy_; }
    const Router::InternalRedirectPolicy& internalRedirectPolicy() const override {
      return internal_redirect_policy_;
    }
    const Router::PathMatcherSharedPtr& pathMatcher() const override { return path_matcher_; }
    const Router::PathRewriterSharedPtr& pathRewriter() const override { return path_rewriter_; }
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
    bool usingNewTimeouts() const override { return false; }
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
    bool appendXfh() const override { return false; }
    bool includeVirtualHostRateLimits() const override { return true; }
    const Router::PathMatchCriterion& pathMatchCriterion() const override {
      return path_match_criterion_;
    }

    const absl::optional<ConnectConfig>& connectConfig() const override {
      return connect_config_nullopt_;
    }

    bool includeAttemptCountInRequest() const override { return false; }
    bool includeAttemptCountInResponse() const override { return false; }
    const Router::RouteEntry::UpgradeMap& upgradeMap() const override { return upgrade_map_; }
    const std::string& routeName() const override { return route_name_; }
    const Router::EarlyDataPolicy& earlyDataPolicy() const override { return *early_data_policy_; }

    std::unique_ptr<const HashPolicyImpl> hash_policy_;
    std::unique_ptr<Router::RetryPolicy> retry_policy_;

    static const NullHedgePolicy hedge_policy_;
    static const NullRateLimitPolicy rate_limit_policy_;
    static const Router::InternalRedirectPolicyImpl internal_redirect_policy_;
    static const Router::PathMatcherSharedPtr path_matcher_;
    static const Router::PathRewriterSharedPtr path_rewriter_;
    static const std::vector<Router::ShadowPolicyPtr> shadow_policies_;
    static const NullVirtualHost virtual_host_;
    static const std::multimap<std::string, std::string> opaque_config_;
    static const NullPathMatchCriterion path_match_criterion_;

    Router::RouteEntry::UpgradeMap upgrade_map_;
    const std::string& cluster_name_;
    absl::optional<std::chrono::milliseconds> timeout_;
    static const absl::optional<ConnectConfig> connect_config_nullopt_;
    const std::string route_name_;
    // Pass early data option config through StreamOptions.
    std::unique_ptr<Router::EarlyDataPolicy> early_data_policy_{
        new Router::DefaultEarlyDataPolicy(true)};
  };

  struct RouteImpl : public Router::Route {
    RouteImpl(AsyncClientImpl& parent, const absl::optional<std::chrono::milliseconds>& timeout,
              const Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy>&
                  hash_policy,
              const absl::optional<envoy::config::route::v3::RetryPolicy>& retry_policy)
        : route_entry_(parent, timeout, hash_policy, retry_policy), typed_metadata_({}) {}

    // Router::Route
    const Router::DirectResponseEntry* directResponseEntry() const override { return nullptr; }
    const Router::RouteEntry* routeEntry() const override { return &route_entry_; }
    const Router::Decorator* decorator() const override { return nullptr; }
    const Router::RouteTracing* tracingConfig() const override { return nullptr; }
    const Router::RouteSpecificFilterConfig*
    mostSpecificPerFilterConfig(const std::string&) const override {
      return nullptr;
    }
    void traversePerFilterConfig(
        const std::string&,
        std::function<void(const Router::RouteSpecificFilterConfig&)>) const override {}
    const envoy::config::core::v3::Metadata& metadata() const override { return metadata_; }
    const Envoy::Config::TypedMetadata& typedMetadata() const override { return typed_metadata_; }

    RouteEntryImpl route_entry_;
    const envoy::config::core::v3::Metadata metadata_;
    const Envoy::Config::TypedMetadataImpl<Envoy::Config::TypedMetadataFactory> typed_metadata_;
  };

  void cleanup();
  void closeRemote(bool end_stream);
  bool complete() { return local_closed_ && remote_closed_; }

  // Http::StreamDecoderFilterCallbacks
  OptRef<const Network::Connection> connection() override { return {}; }
  Event::Dispatcher& dispatcher() override { return parent_.dispatcher_; }
  void resetStream(Http::StreamResetReason reset_reason = Http::StreamResetReason::LocalReset,
                   absl::string_view transport_failure_reason = "") override;
  Router::RouteConstSharedPtr route() override { return route_; }
  Upstream::ClusterInfoConstSharedPtr clusterInfo() override { return parent_.cluster_; }
  uint64_t streamId() const override { return stream_id_; }
  // TODO(kbaichoo): Plumb account from owning request filter.
  Buffer::BufferMemoryAccountSharedPtr account() const override { return nullptr; }
  Tracing::Span& activeSpan() override { return active_span_; }
  OptRef<const Tracing::Config> tracingConfig() const override {
    return makeOptRef<const Tracing::Config>(tracing_config_);
  }
  void continueDecoding() override {}
  RequestTrailerMap& addDecodedTrailers() override { PANIC("not implemented"); }
  void addDecodedData(Buffer::Instance&, bool) override {
    // This should only be called if the user has set up buffering. The request is already fully
    // buffered. Note that this is only called via the async client's internal use of the router
    // filter which uses this function for buffering.
    ASSERT(buffered_body_ != nullptr);
  }
  MetadataMapVector& addDecodedMetadata() override { PANIC("not implemented"); }
  void injectDecodedDataToFilterChain(Buffer::Instance&, bool) override {}
  const Buffer::Instance* decodingBuffer() override { return buffered_body_.get(); }
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {}
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
  // The async client won't pause if sending 1xx headers so simply swallow any.
  void encode1xxHeaders(ResponseHeaderMapPtr&&) override {}
  ResponseHeaderMapOptRef informationalHeaders() const override { return {}; }
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override;
  ResponseHeaderMapOptRef responseHeaders() const override { return {}; }
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override;
  ResponseTrailerMapOptRef responseTrailers() const override { return {}; }
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
  bool recreateStream(const ResponseHeaderMap*) override { return false; }
  const ScopeTrackedObject& scope() override { return *this; }
  void restoreContextOnContinue(ScopeTrackedObjectStack& tracked_object_stack) override {
    tracked_object_stack.add(*this);
  }
  void addUpstreamSocketOptions(const Network::Socket::OptionsSharedPtr&) override {}
  Network::Socket::OptionsSharedPtr getUpstreamSocketOptions() const override { return {}; }
  const Router::RouteSpecificFilterConfig* mostSpecificPerFilterConfig() const override {
    return nullptr;
  }
  void traversePerFilterConfig(
      std::function<void(const Router::RouteSpecificFilterConfig&)>) const override {}
  Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() override { return {}; }
  OptRef<DownstreamStreamFilterCallbacks> downstreamCallbacks() override { return {}; }
  OptRef<UpstreamStreamFilterCallbacks> upstreamCallbacks() override { return {}; }
  void resetIdleTimer() override {}
  void setUpstreamOverrideHost(absl::string_view) override {}
  absl::optional<absl::string_view> upstreamOverrideHost() const override { return {}; }

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
  void modifyDecodingBuffer(std::function<void(Buffer::Instance&)>) override {}

  RequestMessagePtr request_;
  AsyncClient::Callbacks& callbacks_;
  std::unique_ptr<ResponseMessageImpl> response_;
  bool cancelled_{};
  Tracing::SpanPtr child_span_;

  friend class AsyncClientImpl;
};

} // namespace Http
} // namespace Envoy
