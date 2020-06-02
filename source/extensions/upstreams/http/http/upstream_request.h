#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/router/upstream_request.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

class HttpConnPool : public Router::GenericConnPool, public Envoy::Http::ConnectionPool::Callbacks {
public:
  // GenericConnPool
  bool initialize(Upstream::ClusterManager& cm, const Router::RouteEntry& route_entry,
                  Envoy::Http::Protocol protocol, Upstream::LoadBalancerContext* ctx) override {
    conn_pool_ =
        cm.httpConnPoolForCluster(route_entry.clusterName(), route_entry.priority(), protocol, ctx);
    return conn_pool_ != nullptr;
  }
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingRequest() override;
  absl::optional<Envoy::Http::Protocol> protocol() const override;

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo& info) override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

private:
  // Points to the actual connection pool to create streams from.
  Envoy::Http::ConnectionPool::Instance* conn_pool_{};
  Envoy::Http::ConnectionPool::Cancellable* conn_pool_stream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
};

class HttpUpstream : public Router::GenericUpstream, public Envoy::Http::StreamCallbacks {
public:
  HttpUpstream(Router::UpstreamRequest& upstream_request, Envoy::Http::RequestEncoder* encoder)
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
  void encodeHeaders(const Envoy::Http::RequestHeaderMap& headers, bool end_stream) override {
    request_encoder_->encodeHeaders(headers, end_stream);
  }
  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override {
    request_encoder_->encodeTrailers(trailers);
  }

  void readDisable(bool disable) override { request_encoder_->getStream().readDisable(disable); }

  void resetStream() override {
    request_encoder_->getStream().removeCallbacks(*this);
    request_encoder_->getStream().resetStream(Envoy::Http::StreamResetReason::LocalReset);
  }

  // Http::StreamCallbacks
  void onResetStream(Envoy::Http::StreamResetReason reason,
                     absl::string_view transport_failure_reason) override {
    upstream_request_.onResetStream(reason, transport_failure_reason);
  }

  void onAboveWriteBufferHighWatermark() override {
    upstream_request_.disableDataFromDownstreamForFlowControl();
  }

  void onBelowWriteBufferLowWatermark() override {
    upstream_request_.enableDataFromDownstreamForFlowControl();
  }

private:
  Router::UpstreamRequest& upstream_request_;
  Envoy::Http::RequestEncoder* request_encoder_{};
};

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
