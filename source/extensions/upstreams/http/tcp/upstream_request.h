#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

class TcpConnPool : public Router::GenericConnPool, public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
              Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx) {
    conn_pool_data_ = thread_local_cluster.tcpConnPool(priority, ctx);
  }
  // Router::GenericConnPool
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    upstream_handle_ = conn_pool_data_.value().newConnection(*this);
  }
  bool cancelAnyPendingStream() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  Upstream::HostDescriptionConstSharedPtr host() const override {
    return conn_pool_data_.value().host();
  }
  bool valid() const override { return conn_pool_data_.has_value(); }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, transport_failure_reason, host);
  }

  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  absl::optional<Envoy::Upstream::TcpPoolData> conn_pool_data_;
  Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
};

class TcpUpstream : public Router::GenericUpstream,
                    public Envoy::Tcp::ConnectionPool::UpstreamCallbacks {
public:
  TcpUpstream(Router::UpstreamToDownstream* upstream_request,
              Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap&) override;
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;
  const StreamInfo::BytesMeterSharedPtr& bytesMeter() override { return bytes_meter_; }

private:
  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
};

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
