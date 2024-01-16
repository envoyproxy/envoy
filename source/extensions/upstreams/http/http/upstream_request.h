#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

class HttpConnPool : public Router::GenericConnPool, public Envoy::Http::ConnectionPool::Callbacks {
public:
  HttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
               Upstream::ResourcePriority priority,
               absl::optional<Envoy::Http::Protocol> downstream_protocol,
               Upstream::LoadBalancerContext* ctx) {
    pool_data_ = thread_local_cluster.httpConnPool(priority, downstream_protocol, ctx);
  }
  ~HttpConnPool() override {
    ASSERT(conn_pool_stream_handle_ == nullptr, "conn_pool_stream_handle not null");
  }
  // GenericConnPool
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingStream() override;
  bool valid() const override { return pool_data_.has_value(); }
  Upstream::HostDescriptionConstSharedPtr host() const override {
    return pool_data_.value().host();
  }

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host, StreamInfo::StreamInfo& info,
                   absl::optional<Envoy::Http::Protocol> protocol) override;

protected:
  // Points to the actual connection pool to create streams from.
  absl::optional<Envoy::Upstream::HttpPoolData> pool_data_{};
  Envoy::Http::ConnectionPool::Cancellable* conn_pool_stream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
};

class HttpUpstream : public Router::GenericUpstream, public Envoy::Http::StreamCallbacks {
public:
  HttpUpstream(Router::UpstreamToDownstream& upstream_request, Envoy::Http::RequestEncoder* encoder)
      : upstream_request_(upstream_request), request_encoder_(encoder) {
    request_encoder_->getStream().addCallbacks(*this);
  }

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override {
    request_encoder_->encodeData(data, end_stream);
  }
  void encodeMetadata(const Envoy::Http::MetadataMapVector& metadata_map_vector) override {
    request_encoder_->encodeMetadata(metadata_map_vector);
  }
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                    bool end_stream) override {
    return request_encoder_->encodeHeaders(headers, end_stream);
  }
  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override {
    request_encoder_->encodeTrailers(trailers);
  }

  void readDisable(bool disable) override { request_encoder_->getStream().readDisable(disable); }

  void resetStream() override {
    auto& stream = request_encoder_->getStream();
    stream.removeCallbacks(*this);
    stream.resetStream(Envoy::Http::StreamResetReason::LocalReset);
  }

  void setAccount(Buffer::BufferMemoryAccountSharedPtr account) override {
    request_encoder_->getStream().setAccount(std::move(account));
  }

  // Http::StreamCallbacks
  void onResetStream(Envoy::Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override {
    upstream_request_.onResetStream(reason, transport_failure_reason);
  }

  void onAboveWriteBufferHighWatermark() override {
    upstream_request_.onAboveWriteBufferHighWatermark();
  }

  void onBelowWriteBufferLowWatermark() override {
    upstream_request_.onBelowWriteBufferLowWatermark();
  }

  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override {
    return request_encoder_->getStream().bytesMeter();
  }

private:
  Router::UpstreamToDownstream& upstream_request_;
  Envoy::Http::RequestEncoder* request_encoder_{};
};

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
