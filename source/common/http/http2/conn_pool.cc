#include "common/http/http2/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/network/utility.h"
#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : ConnPoolImplBase(std::move(host), std::move(priority)), dispatcher_(dispatcher),
      socket_options_(options), transport_socket_options_(transport_socket_options) {}

ConnPoolImpl::~ConnPoolImpl() {
  if (primary_client_) {
    primary_client_->client_->close();
  }

  if (draining_client_) {
    draining_client_->client_->close();
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImpl::ConnPoolImpl::drainConnections() {
  if (primary_client_ != nullptr) {
    movePrimaryClientToDraining();
  }
}

void ConnPoolImpl::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

bool ConnPoolImpl::hasActiveConnections() const {
  if (primary_client_ && primary_client_->client_->numActiveRequests() > 0) {
    return true;
  }

  if (draining_client_ && draining_client_->client_->numActiveRequests() > 0) {
    return true;
  }

  return !pending_requests_.empty();
}

void ConnPoolImpl::checkForDrained() {
  if (drained_callbacks_.empty()) {
    return;
  }

  bool drained = true;
  if (primary_client_) {
    if (primary_client_->client_->numActiveRequests() == 0) {
      primary_client_->client_->close();
      ASSERT(!primary_client_);
    } else {
      drained = false;
    }
  }

  ASSERT(!draining_client_ || (draining_client_->client_->numActiveRequests() > 0));
  if (draining_client_ && draining_client_->client_->numActiveRequests() > 0) {
    drained = false;
  }

  if (drained) {
    ENVOY_LOG(debug, "invoking drained callbacks");
    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImpl::newClientStream(Http::StreamDecoder& response_decoder,
                                   ConnectionPool::Callbacks& callbacks) {
  if (!host_->cluster().resourceManager(priority_).requests().canCreate()) {
    ENVOY_LOG(debug, "max requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
  } else {
    ENVOY_CONN_LOG(debug, "creating stream", *primary_client_->client_);
    primary_client_->total_streams_++;
    host_->stats().rq_total_.inc();
    host_->stats().rq_active_.inc();
    host_->cluster().stats().upstream_rq_total_.inc();
    host_->cluster().stats().upstream_rq_active_.inc();
    host_->cluster().resourceManager(priority_).requests().inc();
    callbacks.onPoolReady(primary_client_->client_->newStream(response_decoder),
                          primary_client_->real_host_description_,
                          primary_client_->client_->streamInfo());
  }
}

ConnectionPool::Cancellable* ConnPoolImpl::newStream(Http::StreamDecoder& response_decoder,
                                                     ConnectionPool::Callbacks& callbacks) {
  ASSERT(drained_callbacks_.empty());

  // First see if we need to handle max streams rollover.
  uint64_t max_streams = host_->cluster().maxRequestsPerConnection();
  if (max_streams == 0) {
    max_streams = maxTotalStreams();
  }

  if (primary_client_ && primary_client_->total_streams_ >= max_streams) {
    movePrimaryClientToDraining();
  }

  if (!primary_client_) {
    primary_client_ = std::make_unique<ActiveClient>(*this);
  }

  // If the primary client is not connected yet, queue up the request.
  if (!primary_client_->upstream_ready_) {
    // If we're not allowed to enqueue more requests, fail fast.
    if (!host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
      ENVOY_LOG(debug, "max pending requests overflow");
      callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                              nullptr);
      host_->cluster().stats().upstream_rq_pending_overflow_.inc();
      return nullptr;
    }

    return newPendingRequest(response_decoder, callbacks);
  }

  // We already have an active client that's connected to upstream, so attempt to establish a
  // new stream.
  newClientStream(response_decoder, callbacks);
  return nullptr;
}

void ConnPoolImpl::onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_CONN_LOG(debug, "client disconnected", *client.client_);

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    if (client.closed_with_active_rq_) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    if (client.connect_timer_) {
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      // NOTE: We move the existing pending requests to a temporary list. This is done so that
      //       if retry logic submits a new request to the pool, we don't fail it inline.
      purgePendingRequests(client.real_host_description_,
                           client.client_->connectionFailureReason());
    }

    if (&client == primary_client_.get()) {
      ENVOY_CONN_LOG(debug, "destroying primary client", *client.client_);
      dispatcher_.deferredDelete(std::move(primary_client_));
    } else {
      ENVOY_CONN_LOG(debug, "destroying draining client", *client.client_);
      dispatcher_.deferredDelete(std::move(draining_client_));
    }

    if (client.closed_with_active_rq_) {
      checkForDrained();
    }
  }

  if (event == Network::ConnectionEvent::Connected) {
    conn_connect_ms_->complete();

    client.upstream_ready_ = true;
    onUpstreamReady();
  }

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }
}

void ConnPoolImpl::movePrimaryClientToDraining() {
  ENVOY_CONN_LOG(debug, "moving primary to draining", *primary_client_->client_);
  if (draining_client_) {
    // This should pretty much never happen, but is possible if we start draining and then get
    // a goaway for example. In this case just kill the current draining connection. It's not
    // worth keeping a list.
    draining_client_->client_->close();
  }

  ASSERT(!draining_client_);
  if (primary_client_->client_->numActiveRequests() == 0) {
    // If we are making a new connection and the primary does not have any active requests just
    // close it now.
    primary_client_->client_->close();
  } else {
    draining_client_ = std::move(primary_client_);
  }

  ASSERT(!primary_client_);
}

void ConnPoolImpl::onConnectTimeout(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "connect timeout", *client.client_);
  host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  client.client_->close();
}

void ConnPoolImpl::onGoAway(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "remote goaway", *client.client_);
  host_->cluster().stats().upstream_cx_close_notify_.inc();
  if (&client == primary_client_.get()) {
    movePrimaryClientToDraining();
  }
}

void ConnPoolImpl::onStreamDestroy(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", *client.client_,
                 client.client_->numActiveRequests());
  host_->stats().rq_active_.dec();
  host_->cluster().stats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  if (&client == draining_client_.get() && client.client_->numActiveRequests() == 0) {
    // Close out the draining client if we no long have active requests.
    client.client_->close();
  }

  // If we are destroying this stream because of a disconnect, do not check for drain here. We will
  // wait until the connection has been fully drained of streams and then check in the connection
  // event callback.
  if (!client.closed_with_active_rq_) {
    checkForDrained();
  }
}

void ConnPoolImpl::onStreamReset(ActiveClient& client, Http::StreamResetReason reason) {
  if (reason == StreamResetReason::ConnectionTermination ||
      reason == StreamResetReason::ConnectionFailure) {
    host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    client.closed_with_active_rq_ = true;
  } else if (reason == StreamResetReason::LocalReset) {
    host_->cluster().stats().upstream_rq_tx_reset_.inc();
  } else if (reason == StreamResetReason::RemoteReset) {
    host_->cluster().stats().upstream_rq_rx_reset_.inc();
  }
}

void ConnPoolImpl::onUpstreamReady() {
  // Establishes new codec streams for each pending request.
  while (!pending_requests_.empty()) {
    newClientStream(pending_requests_.back()->decoder_, pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : parent_(parent),
      connect_timer_(parent_.dispatcher_.createTimer([this]() -> void { onConnectTimeout(); })) {
  parent_.conn_connect_ms_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_connect_ms_, parent_.dispatcher_.timeSource());
  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;
  client_ = parent_.createCodecClient(data);
  client_->addConnectionCallbacks(*this);
  client_->setCodecClientCallbacks(*this);
  client_->setCodecConnectionCallbacks(*this);
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());

  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http2_total_.inc();
  conn_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      parent_.host_->cluster().stats().upstream_cx_length_ms_, parent_.dispatcher_.timeSource());

  client_->setConnectionStats({parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
                               parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
                               parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
                               parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
                               &parent_.host_->cluster().stats().bind_errors_, nullptr});
}

ConnPoolImpl::ActiveClient::~ActiveClient() {
  parent_.host_->stats().cx_active_.dec();
  parent_.host_->cluster().stats().upstream_cx_active_.dec();
  conn_length_->complete();
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP2, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

uint32_t ProdConnPoolImpl::maxTotalStreams() { return MAX_STREAMS; }

} // namespace Http2
} // namespace Http
} // namespace Envoy
