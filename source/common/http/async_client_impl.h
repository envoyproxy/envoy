#pragma once

#include "message_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"
#include "envoy/router/shadow_writer.h"

#include "common/common/empty_string.h"
#include "common/common/linked_object.h"
#include "common/http/access_log/request_info_impl.h"
#include "common/router/router.h"

namespace Http {

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
                const Optional<std::chrono::milliseconds>& timeout) override;

private:
  const Upstream::ClusterInfo& cluster_;
  Router::FilterConfig config_;
  Event::Dispatcher& dispatcher_;
  std::list<std::unique_ptr<AsyncRequestImpl>> active_requests_;

  friend class AsyncRequestImpl;
};

/**
 * Implementation of AsyncRequest. This implementation is capable of sending HTTP requests to a
 * ConnectionPool asynchronously.
 */
class AsyncRequestImpl final : public AsyncClient::Request,
                               StreamDecoderFilterCallbacks,
                               Router::StableRouteTable,
                               Logger::Loggable<Logger::Id::http>,
                               LinkedObject<AsyncRequestImpl> {
public:
  AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent, AsyncClient::Callbacks& callbacks,
                   const Optional<std::chrono::milliseconds>& timeout);
  ~AsyncRequestImpl();

  // Http::AsyncHttpRequest
  void cancel() override;

private:
  struct NullRateLimitPolicy : public Router::RateLimitPolicy {
    // Router::RateLimitPolicy
    const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>&
        getApplicableRateLimit(int64_t) const override {
      return rate_limit_policy_entry_;
    }

    static const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
        rate_limit_policy_entry_;
  };

  struct NullRetryPolicy : public Router::RetryPolicy {
    // Router::RetryPolicy
    uint32_t numRetries() const override { return 0; }
    uint32_t retryOn() const override { return 0; }
  };

  struct NullShadowPolicy : public Router::ShadowPolicy {
    // Router::ShadowPolicy
    const std::string& cluster() const override { return EMPTY_STRING; }
    const std::string& runtimeKey() const override { return EMPTY_STRING; }
  };

  struct NullVirtualHost : public Router::VirtualHost {
    // Router::VirtualHost
    const std::string& name() const override { return EMPTY_STRING; }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }

    static const NullRateLimitPolicy rate_limit_policy_;
  };

  struct RouteEntryImpl : public Router::RouteEntry {
    RouteEntryImpl(const std::string& cluster_name,
                   const Optional<std::chrono::milliseconds>& timeout)
        : cluster_name_(cluster_name), timeout_(timeout) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    void finalizeRequestHeaders(Http::HeaderMap&) const override {}
    Upstream::ResourcePriority priority() const override {
      return Upstream::ResourcePriority::Default;
    }
    const Router::RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
    const Router::RetryPolicy& retryPolicy() const override { return retry_policy_; }
    const Router::ShadowPolicy& shadowPolicy() const override { return shadow_policy_; }
    std::chrono::milliseconds timeout() const override {
      if (timeout_.valid()) {
        return timeout_.value();
      } else {
        return std::chrono::milliseconds(0);
      }
    }
    const Router::VirtualCluster* virtualCluster(const Http::HeaderMap&) const override {
      return nullptr;
    }
    const Router::VirtualHost& virtualHost() const override { return virtual_host_; }

    static const NullRateLimitPolicy rate_limit_policy_;
    static const NullRetryPolicy retry_policy_;
    static const NullShadowPolicy shadow_policy_;
    static const NullVirtualHost virtual_host_;

    const std::string& cluster_name_;
    Optional<std::chrono::milliseconds> timeout_;
  };

  struct RouteImpl : public Router::Route {
    RouteImpl(const std::string& cluster_name, const Optional<std::chrono::milliseconds>& timeout)
        : route_entry_(cluster_name, timeout) {}

    // Router::Route
    const Router::RedirectEntry* redirectEntry() const override { return nullptr; }
    const Router::RouteEntry* routeEntry() const override { return &route_entry_; }

    RouteEntryImpl route_entry_;
  };

  void cleanup();
  void failDueToClientDestroy();
  void onComplete();

  // Http::StreamDecoderFilterCallbacks
  void addResetStreamCallback(std::function<void()> callback) override {
    reset_callback_ = callback;
  }
  uint64_t connectionId() override { return 0; }
  Event::Dispatcher& dispatcher() override { return parent_.dispatcher_; }
  void resetStream() override;
  const Router::StableRouteTable& routeTable() { return *this; }
  uint64_t streamId() override { return stream_id_; }
  AccessLog::RequestInfo& requestInfo() override { return request_info_; }
  const std::string& downstreamAddress() override { return EMPTY_STRING; }
  void continueDecoding() override { NOT_IMPLEMENTED; }
  const Buffer::Instance* decodingBuffer() override { return request_->body(); }
  void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(HeaderMapPtr&& trailers) override;

  // Router::StableRouteTable
  const Router::Route* route(const Http::HeaderMap&) const override { return &route_; }

  MessagePtr request_;
  AsyncClientImpl& parent_;
  AsyncClient::Callbacks& callbacks_;
  const uint64_t stream_id_;
  std::unique_ptr<MessageImpl> response_;
  Router::ProdFilter router_;
  std::function<void()> reset_callback_;
  AccessLog::RequestInfoImpl request_info_;
  RouteImpl route_;
  bool complete_{};

  friend class AsyncClientImpl;
};

} // Http
