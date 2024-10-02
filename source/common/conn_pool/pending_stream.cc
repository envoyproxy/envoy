#include "source/common/conn_pool/pending_stream.h"

#include <chrono>

#include "source/common/conn_pool/conn_pool_base.h"

namespace Envoy {
namespace ConnectionPool {

PendingStream::PendingStream(ConnPoolImplBase& parent, bool can_send_early_data)
    : parent_(parent), can_send_early_data_(can_send_early_data),
      enqueued_ts_(parent.dispatcher().timeSource().monotonicTime()) {
  Upstream::ClusterTrafficStats& traffic_stats = *parent_.host()->cluster().trafficStats();
  traffic_stats.upstream_rq_pending_total_.inc();
  traffic_stats.upstream_rq_pending_active_.inc();
  parent_.host()->cluster().resourceManager(parent_.priority()).pendingRequests().inc();
}

PendingStream::~PendingStream() {
  parent_.host()->cluster().trafficStats()->upstream_rq_pending_active_.dec();
  parent_.host()->cluster().resourceManager(parent_.priority()).pendingRequests().dec();
}

void PendingStream::cancel(Envoy::ConnectionPool::CancelPolicy policy) {
  parent_.onPendingStreamCancel(*this, policy);
}

} // namespace ConnectionPool
} // namespace Envoy