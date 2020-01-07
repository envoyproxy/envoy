#include "common/http/http2/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : ConnPoolImplBase(std::move(host), std::move(priority), dispatcher), socket_options_(options),
      transport_socket_options_(transport_socket_options) {}

void ConnPoolImpl::newClientStream(ActiveClient& client, Http::StreamDecoder& response_decoder,
                                   ConnectionPool::Callbacks& callbacks) {
  if (!host_->cluster().resourceManager(priority_).requests().canCreate()) {
    ENVOY_LOG(debug, "max requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
  } else {
    ENVOY_CONN_LOG(debug, "creating stream", *client.codec_client_);
    client.total_streams_++;
    host_->stats().rq_total_.inc();
    host_->stats().rq_active_.inc();
    host_->cluster().stats().upstream_rq_total_.inc();
    host_->cluster().stats().upstream_rq_active_.inc();
    host_->cluster().resourceManager(priority_).requests().inc();
    callbacks.onPoolReady(client.codec_client_->newStream(response_decoder),
                          client.real_host_description_, client.codec_client_->streamInfo());
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

  if (primaryClient() && primaryClient()->total_streams_ >= max_streams) {
    movePrimaryClientToDraining();
  }

  if (!primaryClient()) {
    auto client = std::make_unique<ActiveClient>(*this);
    client->moveIntoList(std::move(client), busy_clients_);
  }

  // If there are no ready clients yet, queue up the request.
  if (ready_clients_.empty()) {
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
  newClientStream(*primaryClient(), response_decoder, callbacks);
  return nullptr;
}

void ConnPoolImpl::movePrimaryClientToDraining() {
  ENVOY_CONN_LOG(debug, "moving primary to draining", *primaryClient()->codec_client_);
  if (primaryClient()->codec_client_->numActiveRequests() == 0) {
    // If we are making a new connection and the primary does not have any active requests just
    // close it now.
    primaryClient()->codec_client_->close();
  } else {
    // TODO(ggreenway): get correct source list
    primaryClient()->moveBetweenLists(busy_clients_, draining_clients_);
  }

  ASSERT(!primaryClient());
}

void ConnPoolImpl::onGoAway(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "remote goaway", *client.codec_client_);
  host_->cluster().stats().upstream_cx_close_notify_.inc();
  if (client.state_ == ConnPoolImplBase::ActiveClient::State::READY ||
      client.state_ == ConnPoolImplBase::ActiveClient::State::BUSY) {
    movePrimaryClientToDraining();
  }
}

void ConnPoolImpl::onStreamDestroy(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", *client.codec_client_,
                 client.codec_client_->numActiveRequests());
  host_->stats().rq_active_.dec();
  host_->cluster().stats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  if (client.state_ == ActiveClient::State::DRAINING &&
      client.codec_client_->numActiveRequests() == 0) {
    // Close out the draining client if we no long have active requests.
    client.codec_client_->close();
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
  while (!pending_requests_.empty() && primaryClient() != nullptr) {
    newClientStream(*primaryClient(), pending_requests_.back()->decoder_,
                    pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : ConnPoolImplBase::ActiveClient(parent), parent_(parent) {
  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;
  codec_client_ = parent_.createCodecClient(data);
  codec_client_->addConnectionCallbacks(*this);
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);

  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http2_total_.inc();

  codec_client_->setConnectionStats(
      {parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &parent_.host_->cluster().stats().bind_errors_, nullptr});
}

ConnPoolImpl::ActiveClient::~ActiveClient() {
  parent_.host_->stats().cx_active_.dec();
  parent_.host_->cluster().stats().upstream_cx_active_.dec();
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
