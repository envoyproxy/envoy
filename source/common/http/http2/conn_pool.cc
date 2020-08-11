#include "common/http/http2/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {
namespace Http2 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : HttpConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                           transport_socket_options, Protocol::Http2) {}

ConnPoolImpl::~ConnPoolImpl() { destructAllConnections(); }

Envoy::ConnectionPool::ActiveClientPtr ConnPoolImpl::instantiateActiveClient() {
  return std::make_unique<ActiveClient>(*this);
}
void ConnPoolImpl::onGoAway(ActiveClient& client, Http::GoAwayErrorCode) {
  ENVOY_CONN_LOG(debug, "remote goaway", *client.codec_client_);
  host_->cluster().stats().upstream_cx_close_notify_.inc();
  if (client.state_ != ActiveClient::State::DRAINING) {
    if (client.codec_client_->numActiveRequests() == 0) {
      client.codec_client_->close();
    } else {
      transitionActiveClientState(client, ActiveClient::State::DRAINING);
    }
  }
}

void ConnPoolImpl::onStreamDestroy(ActiveClient& client) {
  onRequestClosed(client, false);

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

uint64_t ConnPoolImpl::maxRequestsPerConnection() {
  uint64_t max_streams_config = host_->cluster().maxRequestsPerConnection();
  return (max_streams_config != 0) ? max_streams_config : DEFAULT_MAX_STREAMS;
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : Envoy::Http::ActiveClient(
          parent, parent.maxRequestsPerConnection(),
          parent.host_->cluster().http2Options().max_concurrent_streams().value()) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);

  parent.host_->cluster().stats().upstream_cx_http2_total_.inc();
}

bool ConnPoolImpl::ActiveClient::closingWithIncompleteRequest() const {
  return closed_with_active_rq_;
}

RequestEncoder& ConnPoolImpl::ActiveClient::newStreamEncoder(ResponseDecoder& response_decoder) {
  return codec_client_->newStream(response_decoder);
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP2, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                 Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options) {
  return std::make_unique<Http::Http2::ProdConnPoolImpl>(dispatcher, host, priority, options,
                                                         transport_socket_options);
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
