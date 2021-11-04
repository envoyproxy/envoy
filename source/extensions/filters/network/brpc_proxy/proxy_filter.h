#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/brpc_proxy/request_manager.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

/**
 * All brpc proxy stats. @see stats_macros.h
 */
#define ALL_BRPC_PROXY_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  COUNTER(downstream_rq_total)                                                                     \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_cx_rx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_cx_tx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_rq_active, Accumulate)

/**
 * Struct definition for all brpc proxy stats. @see stats_macros.h
 */
struct BrpcProxyStats {
  ALL_BRPC_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

using BrpcProxyStatsSharedPtr = std::shared_ptr<BrpcProxyStats>;

BrpcProxyStatsSharedPtr generateStats(const std::string& prefix, Stats::Scope& scope);
/**
 * A brpc proxy filter. This filter will take incoming brpc request, and
 * dispatch them to cluster specified in request meta info.
 */
class ProxyFilter : public Network::ReadFilter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks {
public:
  ProxyFilter(BrpcProxy::DecoderFactory& factory, EncoderPtr&& encoder,
              RequestManager::RMInstance& manager, BrpcProxyStatsSharedPtr stats);
  ~ProxyFilter() override;

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  //DecoderCallbacks
  void onMessage(BrpcMessagePtr&& value) override;



private:
  struct PendingRequest : public PoolCallbacks {
    PendingRequest(ProxyFilter& parent, int64_t request_id);
    ~PendingRequest() override;
    void onResponse(BrpcMessagePtr&& value) override {
      parent_.onResponse(*this, std::move(value));
    }

    ProxyFilter& parent_;
    BrpcMessagePtr pending_response_;
    BrpcRequestPtr request_handle_;
	int64_t request_id_;
  };

  void onResponse(PendingRequest& request, BrpcMessagePtr&& value);

  DecoderPtr decoder_;
  EncoderPtr encoder_;
  RequestManager::RMInstance& manager_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
  BrpcProxyStatsSharedPtr stat_;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

