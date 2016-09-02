#pragma once

#include "message_impl.h"
#include "pooled_stream_encoder.h"

#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "common/common/assert.h"
#include "common/common/linked_object.h"
#include "common/http/header_map_impl.h"

namespace Http {

/**
 * Factory for obtaining a connection pool.
 */
class AsyncClientConnPoolFactory {
public:
  virtual ~AsyncClientConnPoolFactory() {}

  /**
   * Return a connection pool or nullptr if there is no healthy upstream host.
   */
  virtual ConnectionPool::Instance* connPool(Upstream::ResourcePriority priority) PURE;
};

class AsyncRequestImpl;

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(const Upstream::Cluster& cluster, AsyncClientConnPoolFactory& factory,
                  Stats::Store& stats_store, Event::Dispatcher& dispatcher,
                  const std::string& local_zone_name);
  ~AsyncClientImpl();

  // Http::AsyncClient
  Request* send(MessagePtr&& request, Callbacks& callbacks,
                const Optional<std::chrono::milliseconds>& timeout) override;

private:
  const Upstream::Cluster& cluster_;
  AsyncClientConnPoolFactory& factory_;
  Stats::Store& stats_store_;
  Event::Dispatcher& dispatcher_;
  const std::string local_zone_name_;
  const std::string stat_prefix_;
  std::list<std::unique_ptr<AsyncRequestImpl>> active_requests_;

  friend class AsyncRequestImpl;
};

/**
 * Implementation of AsyncRequest. This implementation is capable of sending HTTP requests to a
 * ConnectionPool asynchronously.
 */
class AsyncRequestImpl final : public AsyncClient::Request,
                               StreamDecoder,
                               StreamCallbacks,
                               PooledStreamEncoderCallbacks,
                               Logger::Loggable<Logger::Id::http>,
                               LinkedObject<AsyncRequestImpl> {
public:
  AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent, AsyncClient::Callbacks& callbacks,
                   Event::Dispatcher& dispatcher, ConnectionPool::Instance& conn_pool,
                   const Optional<std::chrono::milliseconds>& timeout);
  ~AsyncRequestImpl();

  // Http::AsyncHttpRequest
  void cancel() override;

private:
  const std::string& upstreamZone();
  bool isUpstreamCanary();

  // Http::StreamDecoder
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(const Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(StreamResetReason reason) override;

  // Http::PooledStreamEncoderCallbacks
  void onUpstreamHostSelected(Upstream::HostDescriptionPtr upstream_host) override {
    upstream_host_ = upstream_host;
  }

  void onComplete();

  void onRequestTimeout();
  void cleanup();

  MessagePtr request_;
  AsyncClientImpl& parent_;
  AsyncClient::Callbacks& callbacks_;
  Event::TimerPtr request_timeout_;
  std::unique_ptr<MessageImpl> response_;
  PooledStreamEncoderPtr stream_encoder_;
  Upstream::HostDescriptionPtr upstream_host_;

  static const HeaderMapImpl SERVICE_UNAVAILABLE_HEADER;
  static const HeaderMapImpl REQUEST_TIMEOUT_HEADER;

  friend class AsyncClientImpl;
};

} // Http
