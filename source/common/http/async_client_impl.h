#pragma once

#include "message_impl.h"
#include "pooled_stream_encoder.h"

#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

namespace Http {

class AsyncClientImpl final : public AsyncClient {
public:
  AsyncClientImpl(ConnectionPool::Instance& conn_pool, const std::string& cluster,
                  Stats::Store& stats_store, Event::Dispatcher& dispatcher);

  // Http::AsyncClient
  RequestPtr send(MessagePtr&& request, Callbacks& callbacks,
                  const Optional<std::chrono::milliseconds>& timeout) override;

private:
  ConnectionPool::Instance& conn_pool_;
  const std::string stat_prefix_;
  Stats::Store& stats_store_;
  Event::Dispatcher& dispatcher_;

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
                               Logger::Loggable<Logger::Id::http> {
public:
  AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent, AsyncClient::Callbacks& callbacks,
                   Event::Dispatcher& dispatcher,
                   const Optional<std::chrono::milliseconds>& timeout);
  ~AsyncRequestImpl();

  // Http::AsyncHttpRequest
  void cancel() override;

private:
  // Http::StreamDecoder
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
  void decodeData(const Buffer::Instance& data, bool end_stream) override;
  void decodeTrailers(HeaderMapPtr&& trailers) override;

  // Http::StreamCallbacks
  void onResetStream(StreamResetReason reason) override;

  // Http::PooledStreamEncoderCallbacks
  void onUpstreamHostSelected(Upstream::HostDescriptionPtr) override {}

  void onComplete();

  void onRequestTimeout();
  void cleanup();

  MessagePtr request_;
  AsyncClientImpl& parent_;
  AsyncClient::Callbacks& callbacks_;
  Event::TimerPtr request_timeout_;
  std::unique_ptr<MessageImpl> response_;
  PooledStreamEncoderPtr stream_encoder_;

  static const Http::HeaderMapImpl SERVICE_UNAVAILABLE_HEADER;
  static const Http::HeaderMapImpl REQUEST_TIMEOUT_HEADER;

  friend class AsyncClientImpl;
};

} // Http
