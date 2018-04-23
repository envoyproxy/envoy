#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"
#include "envoy/ssl/connection.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/empty_string.h"
#include "common/common/linked_object.h"
#include "common/http/message_impl.h"
#include "common/request_info/request_info_impl.h"
#include "common/router/router.h"
#include "common/tracing/http_tracer_impl.h"

namespace Envoy {
namespace Http {

class AsyncStreamImpl;
class AsyncRequestImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(const Upstream::ClusterInfo& cluster, Stats::Store& stats_store,
                  Event::Dispatcher& dispatcher, const LocalInfo::LocalInfo& local_info,
                  Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                  Runtime::RandomGenerator& random, Router::ShadowWriterPtr&& shadow_writer);
  ~AsyncClientImpl();

  // Http::AsyncClient
  Request* send(MessagePtr&& request, Callbacks& callbacks,
                const absl::optional<std::chrono::milliseconds>& timeout) override;

  Stream* start(StreamCallbacks& callbacks,
                const absl::optional<std::chrono::milliseconds>& timeout,
                bool buffer_body_for_retry) override;

  Event::Dispatcher& dispatcher() override { return dispatcher_; }

private:
  const Upstream::ClusterInfo& cluster_;
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
                        LinkedObject<AsyncStreamImpl> {
public:
  AsyncStreamImpl(AsyncClientImpl& parent, AsyncClient::StreamCallbacks& callbacks,
                  const absl::optional<std::chrono::milliseconds>& timeout,
                  bool buffer_body_for_retry);

  // Http::AsyncClient::Stream
  void sendHeaders(HeaderMap& headers, bool end_stream) override;
  void sendData(Buffer::Instance& data, bool end_stream) override;
  void sendTrailers(HeaderMap& trailers) override;
  void reset() override;

protected:
  bool remoteClosed() { return remote_closed_; }

  AsyncClientImpl& parent_;

private:
  struct NullCorsPolicy : public Router::CorsPolicy {
    // Router::CorsPolicy
    const std::list<std::string>& allowOrigins() const override { return allow_origin_; };
    const std::string& allowMethods() const override { return EMPTY_STRING; };
    const std::string& allowHeaders() const override { return EMPTY_STRING; };
    const std::string& exposeHeaders() const override { return EMPTY_STRING; };
    const std::string& maxAge() const override { return EMPTY_STRING; };
    const absl::optional<bool>& allowCredentials() const override { return allow_credentials_; };
    bool enabled() const override { return false; };

    static const std::list<std::string> allow_origin_;
    static const absl::optional<bool> allow_credentials_;
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
    uint32_t numRetries() const override { return 0; }
    uint32_t retryOn() const override { return 0; }
  };

  struct NullShadowPolicy : public Router::ShadowPolicy {
    // Router::ShadowPolicy
    const std::string& cluster() const override { return EMPTY_STRING; }
    const std::string& runtimeKey() const override { return EMPTY_STRING; }
  };

  struct NullConfig : public Router::Config {
    Router::RouteConstSharedPtr route(const Http::HeaderMap&, uint64_t) const override {
      return nullptr;
    }

    const std::list<LowerCaseString>& internalOnlyHeaders() const override {
      return internal_only_headers_;
    }

    const std::string& name() const override { return EMPTY_STRING; }

    static const std::list<LowerCaseString> internal_only_headers_;
  };

  struct NullVirtualHost : public Router::VirtualHost {
    // Router::VirtualHost
    const std::string& name() const override { return EMPTY_STRING; }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    const Router::Config& routeConfig() const override { return route_configuration_; }
    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }

    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullConfig route_configuration_;
  };

  struct NullPathMatchCriterion : public Router::PathMatchCriterion {
    Router::PathMatchType matchType() const override { return Router::PathMatchType::None; }
    const std::string& matcher() const override { return EMPTY_STRING; }
  };

  struct RouteEntryImpl : public Router::RouteEntry {
    RouteEntryImpl(const std::string& cluster_name,
                   const absl::optional<std::chrono::milliseconds>& timeout)
        : cluster_name_(cluster_name), timeout_(timeout) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    Http::Code clusterNotFoundResponseCode() const override {
      return Http::Code::InternalServerError;
    }
    const Router::CorsPolicy* corsPolicy() const override { return nullptr; }
    void finalizeRequestHeaders(Http::HeaderMap&, const RequestInfo::RequestInfo&) const override {}
    void finalizeResponseHeaders(Http::HeaderMap&, const RequestInfo::RequestInfo&) const override {
    }
    const Router::HashPolicy* hashPolicy() const override { return nullptr; }
    const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
    Upstream::ResourcePriority priority() const override {
      return Upstream::ResourcePriority::Default;
    }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::RetryPolicy& retryPolicy() const override { return retry_policy_; }
    const Router::ShadowPolicy& shadowPolicy() const override { return shadow_policy_; }
    std::chrono::milliseconds timeout() const override {
      if (timeout_) {
        return timeout_.value();
      } else {
        return std::chrono::milliseconds(0);
      }
    }
    const Router::VirtualCluster* virtualCluster(const Http::HeaderMap&) const override {
      return nullptr;
    }
    const std::multimap<std::string, std::string>& opaqueConfig() const override {
      return opaque_config_;
    }
    const Router::VirtualHost& virtualHost() const override { return virtual_host_; }
    bool autoHostRewrite() const override { return false; }
    bool useWebSocket() const override { return false; }
    bool includeVirtualHostRateLimits() const override { return true; }
    const envoy::api::v2::core::Metadata& metadata() const override { return metadata_; }
    const Router::PathMatchCriterion& pathMatchCriterion() const override {
      return path_match_criterion_;
    }

    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }

    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullRetryPolicy retry_policy_;
    static const NullShadowPolicy shadow_policy_;
    static const NullVirtualHost virtual_host_;
    static const std::multimap<std::string, std::string> opaque_config_;
    static const envoy::api::v2::core::Metadata metadata_;
    static const NullPathMatchCriterion path_match_criterion_;

    const std::string& cluster_name_;
    absl::optional<std::chrono::milliseconds> timeout_;
  };

  struct RouteImpl : public Router::Route {
    RouteImpl(const std::string& cluster_name,
              const absl::optional<std::chrono::milliseconds>& timeout)
        : route_entry_(cluster_name, timeout) {}

    // Router::Route
    const Router::DirectResponseEntry* directResponseEntry() const override { return nullptr; }
    const Router::RouteEntry* routeEntry() const override { return &route_entry_; }
    const Router::Decorator* decorator() const override { return nullptr; }
    const Router::RouteSpecificFilterConfig* perFilterConfig(const std::string&) const override {
      return nullptr;
    }

    RouteEntryImpl route_entry_;
  };

  void cleanup();
  void closeLocal(bool end_stream);
  void closeRemote(bool end_stream);
  bool complete() { return local_closed_ && remote_closed_; }

  // Http::StreamDecoderFilterCallbacks
  const Network::Connection* connection() override { return nullptr; }
  Event::Dispatcher& dispatcher() override { return parent_.dispatcher_; }
  void resetStream() override;
  Router::RouteConstSharedPtr route() override { return route_; }
  void clearRouteCache() override {}
  uint64_t streamId() override { return stream_id_; }
  RequestInfo::RequestInfo& requestInfo() override { return request_info_; }
  Tracing::Span& activeSpan() override { return active_span_; }
  const Tracing::Config& tracingConfig() override { return tracing_config_; }
  void continueDecoding() override { NOT_IMPLEMENTED; }
  void addDecodedData(Buffer::Instance&, bool) override { NOT_IMPLEMENTED; }
  const Buffer::Instance* decodingBuffer() override { return buffered_body_.get(); }
  // The async client won't pause if sending an Expect: 100-Continue so simply
  // swallows any incoming encode100Continue.
  void encode100ContinueHeaders(HeaderMapPtr&&) override {}
  void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(HeaderMapPtr&& trailers) override;
  void onDecoderFilterAboveWriteBufferHighWatermark() override {}
  void onDecoderFilterBelowWriteBufferLowWatermark() override {}
  void addDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void removeDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks&) override {}
  void setDecoderBufferLimit(uint32_t) override {}
  uint32_t decoderBufferLimit() override { return 0; }

  AsyncClient::StreamCallbacks& stream_callbacks_;
  const uint64_t stream_id_;
  Router::ProdFilter router_;
  RequestInfo::RequestInfoImpl request_info_;
  Tracing::NullSpan active_span_;
  const Tracing::Config& tracing_config_;
  std::shared_ptr<RouteImpl> route_;
  bool local_closed_{};
  bool remote_closed_{};
  Buffer::InstancePtr buffered_body_;
  friend class AsyncClientImpl;
};

class AsyncRequestImpl final : public AsyncClient::Request,
                               AsyncStreamImpl,
                               AsyncClient::StreamCallbacks {
public:
  AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent, AsyncClient::Callbacks& callbacks,
                   const absl::optional<std::chrono::milliseconds>& timeout);

  // AsyncClient::Request
  virtual void cancel() override;

private:
  void initialize();
  void onComplete();

  // AsyncClient::StreamCallbacks
  void onHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void onData(Buffer::Instance& data, bool end_stream) override;
  void onTrailers(HeaderMapPtr&& trailers) override;
  void onReset() override;

  // Http::StreamDecoderFilterCallbacks
  const Buffer::Instance* decodingBuffer() override { return request_->body().get(); }

  MessagePtr request_;
  AsyncClient::Callbacks& callbacks_;
  std::unique_ptr<MessageImpl> response_;
  bool cancelled_{};

  friend class AsyncClientImpl;
};

} // namespace Http
} // namespace Envoy
