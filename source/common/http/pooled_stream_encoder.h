#pragma once

#include "envoy/common/time.h"
#include "envoy/http/codec.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Http {

/**
 * Callbacks emitted by a PooledStreamEncoder.
 */
class PooledStreamEncoderCallbacks {
public:
  virtual ~PooledStreamEncoderCallbacks() {}

  /**
   * Called when the connection pool has selected a host that will service the request. It's
   * possible this is called in the context of failure. It's also possible that host is nullptr
   * in the case that no host could be selected.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionPtr host) PURE;
};

/**
 * A pooled StreamEncoder. Manages buffering of body data until a pool connection becomes
 * available.
 */
class PooledStreamEncoder final : public Logger::Loggable<Logger::Id::pool>,
                                  public ConnectionPool::Callbacks,
                                  public StreamCallbacks {
public:
  PooledStreamEncoder(ConnectionPool::Instance& conn_pool, StreamDecoder& decoder,
                      StreamCallbacks& stream_callbacks, uint64_t connection_id, uint64_t stream_id,
                      PooledStreamEncoderCallbacks& callbacks);

  void encodeHeaders(const HeaderMap& headers, bool end_stream);
  void encodeData(const Buffer::Instance& data, bool end_stream);
  void encodeTrailers(const HeaderMap& trailers);
  void resetStream();
  uint64_t connectionId() { return connection_id_; }
  uint64_t streamId() { return stream_id_; }
  SystemTime requestCompleteTime() { return request_complete_time_; }

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionPtr host) override;
  void onPoolReady(StreamEncoder& request_encoder, Upstream::HostDescriptionPtr host) override;

  // Http::StreamCallbacks
  void onResetStream(StreamResetReason reason) override;

private:
  void commonEncodePrefix(bool end_stream);

  ConnectionPool::Instance& conn_pool_;
  StreamDecoder& stream_decoder_;
  StreamCallbacks& stream_callbacks_;
  const uint64_t connection_id_;
  const uint64_t stream_id_;
  PooledStreamEncoderCallbacks& callbacks_;
  ConnectionPool::Cancellable* conn_pool_stream_handle_{};
  const HeaderMap* request_headers_{};
  const HeaderMap* request_trailers_{};
  bool request_complete_{};
  Buffer::OwnedImpl buffered_request_body_;
  StreamEncoder* request_encoder_{};
  SystemTime request_complete_time_;
  bool calling_encode_headers_{};
  Optional<StreamResetReason> deferred_reset_reason_;
};

typedef std::unique_ptr<PooledStreamEncoder> PooledStreamEncoderPtr;

} // Http