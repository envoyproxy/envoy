#include "common/http/http2/conn_pool.h"

#include <cstdint>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// All streams are 2^31. Client streams are half that, minus stream 0. Just to be on the safe
// side we do 2^29.
static const uint64_t DEFAULT_MAX_STREAMS = (1 << 29);

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                           Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : HttpConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                           transport_socket_options, random_generator, {Protocol::Http2}) {}

ConnPoolImpl::~ConnPoolImpl() { destructAllConnections(); }

Envoy::ConnectionPool::ActiveClientPtr ConnPoolImpl::instantiateActiveClient() {
  return std::make_unique<ActiveClient>(*this);
}

void ConnPoolImpl::ActiveClient::onGoAway(Http::GoAwayErrorCode) {
  ENVOY_CONN_LOG(debug, "remote goaway", *codec_client_);
  parent_.host()->cluster().stats().upstream_cx_close_notify_.inc();
  if (state_ != ActiveClient::State::DRAINING) {
    if (codec_client_->numActiveRequests() == 0) {
      codec_client_->close();
    } else {
      parent_.transitionActiveClientState(*this, ActiveClient::State::DRAINING);
    }
  }
}

void ConnPoolImpl::ActiveClient::onStreamDestroy() {
  parent().onStreamClosed(*this, false);

  // If we are destroying this stream because of a disconnect, do not check for drain here. We will
  // wait until the connection has been fully drained of streams and then check in the connection
  // event callback.
  if (!closed_with_active_rq_) {
    parent().checkForDrained();
  }
}

void ConnPoolImpl::ActiveClient::onStreamReset(Http::StreamResetReason reason) {
  if (reason == StreamResetReason::ConnectionTermination ||
      reason == StreamResetReason::ConnectionFailure) {
    parent_.host()->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    closed_with_active_rq_ = true;
  } else if (reason == StreamResetReason::LocalReset) {
    parent_.host()->cluster().stats().upstream_rq_tx_reset_.inc();
  } else if (reason == StreamResetReason::RemoteReset) {
    parent_.host()->cluster().stats().upstream_rq_rx_reset_.inc();
  }
}

uint64_t maxStreamsPerConnection(uint64_t max_streams_config) {
  return (max_streams_config != 0) ? max_streams_config : DEFAULT_MAX_STREAMS;
}

ConnPoolImpl::ActiveClient::ActiveClient(Envoy::Http::HttpConnPoolImplBase& parent)
    : Envoy::Http::ActiveClient(
          parent, maxStreamsPerConnection(parent.host()->cluster().maxRequestsPerConnection()),
          parent.host()->cluster().http2Options().max_concurrent_streams().value()) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);
  parent.host()->cluster().stats().upstream_cx_http2_total_.inc();
}

bool ConnPoolImpl::ActiveClient::closingWithIncompleteStream() const {
  return closed_with_active_rq_;
}

RequestEncoder& ConnPoolImpl::ActiveClient::newStreamEncoder(ResponseDecoder& response_decoder) {
  return codec_client_->newStream(response_decoder);
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP2, std::move(data.connection_),
                                           data.host_description_, dispatcher_, random_generator_)};
  return codec;
}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options) {
  return std::make_unique<Http::Http2::ProdConnPoolImpl>(
      dispatcher, random_generator, host, priority, options, transport_socket_options);
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
