#include "source/common/http/conn_pool_base.h"

#include "source/common/common/assert.h"
#include "source/common/http/utility.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {

Network::TransportSocketOptionsConstSharedPtr
wrapTransportSocketOptions(Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
                           std::vector<Protocol> protocols) {
  std::vector<std::string> fallbacks;
  for (auto protocol : protocols) {
    // If configured to do so, we override the ALPN to use for the upstream connection to match the
    // selected protocol.
    switch (protocol) {
    case Http::Protocol::Http10:
      NOT_REACHED_GCOVR_EXCL_LINE;
    case Http::Protocol::Http11:
      fallbacks.push_back(Http::Utility::AlpnNames::get().Http11);
      break;
    case Http::Protocol::Http2:
      fallbacks.push_back(Http::Utility::AlpnNames::get().Http2);
      break;
    case Http::Protocol::Http3:
      // HTTP3 ALPN is set in the QUIC stack based on supported versions.
      break;
    }
  }

  if (transport_socket_options) {
    return std::make_shared<Network::AlpnDecoratingTransportSocketOptions>(
        std::move(fallbacks), transport_socket_options);
  } else {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::move(fallbacks));
  }
}

HttpConnPoolImplBase::HttpConnPoolImplBase(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    Random::RandomGenerator& random_generator, Upstream::ClusterConnectivityState& state,
    std::vector<Http::Protocol> protocols)
    : Envoy::ConnectionPool::ConnPoolImplBase(
          host, priority, dispatcher, options,
          wrapTransportSocketOptions(transport_socket_options, protocols), state),
      random_generator_(random_generator) {
  ASSERT(!protocols.empty());
}

HttpConnPoolImplBase::~HttpConnPoolImplBase() { destructAllConnections(); }

ConnectionPool::Cancellable*
HttpConnPoolImplBase::newStream(Http::ResponseDecoder& response_decoder,
                                Http::ConnectionPool::Callbacks& callbacks) {
  HttpAttachContext context({&response_decoder, &callbacks});
  return Envoy::ConnectionPool::ConnPoolImplBase::newStream(context);
}

bool HttpConnPoolImplBase::hasActiveConnections() const {
  return (hasPendingStreams() || (hasActiveStreams()));
}

ConnectionPool::Cancellable*
HttpConnPoolImplBase::newPendingStream(Envoy::ConnectionPool::AttachContext& context) {
  Http::ResponseDecoder& decoder = *typedContext<HttpAttachContext>(context).decoder_;
  Http::ConnectionPool::Callbacks& callbacks = *typedContext<HttpAttachContext>(context).callbacks_;
  ENVOY_LOG(debug, "queueing stream due to no available connections");
  Envoy::ConnectionPool::PendingStreamPtr pending_stream(
      new HttpPendingStream(*this, decoder, callbacks));
  return addPendingStream(std::move(pending_stream));
}

void HttpConnPoolImplBase::onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                                       Envoy::ConnectionPool::AttachContext& context) {
  ActiveClient* http_client = static_cast<ActiveClient*>(&client);
  auto& http_context = typedContext<HttpAttachContext>(context);
  Http::ResponseDecoder& response_decoder = *http_context.decoder_;
  Http::ConnectionPool::Callbacks& callbacks = *http_context.callbacks_;
  Http::RequestEncoder& new_encoder = http_client->newStreamEncoder(response_decoder);
  callbacks.onPoolReady(new_encoder, client.real_host_description_,
                        http_client->codec_client_->streamInfo(),
                        http_client->codec_client_->protocol());
}

// All streams are 2^31. Client streams are half that, minus stream 0. Just to be on the safe
// side we do 2^29.
static const uint64_t DEFAULT_MAX_STREAMS = (1 << 29);

void MultiplexedActiveClientBase::onGoAway(Http::GoAwayErrorCode) {
  ENVOY_CONN_LOG(debug, "remote goaway", *codec_client_);
  parent_.host()->cluster().stats().upstream_cx_close_notify_.inc();
  if (state() != ActiveClient::State::DRAINING) {
    if (codec_client_->numActiveRequests() == 0) {
      codec_client_->close();
    } else {
      parent_.transitionActiveClientState(*this, ActiveClient::State::DRAINING);
    }
  }
}

// Adjust the concurrent stream limit if the negotiated concurrent stream limit
// is lower than the local max configured streams.
//
// Note: if multiple streams are assigned to a connection before the settings
// are received, they may still be reset by the peer. This could be avoided by
// not considering http/2 connections connected until the SETTINGS frame is
// received, but that would result in a latency penalty instead.
void MultiplexedActiveClientBase::onSettings(ReceivedSettings& settings) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.improved_stream_limit_handling") &&
      settings.maxConcurrentStreams().has_value() &&
      settings.maxConcurrentStreams().value() < concurrent_stream_limit_) {
    int64_t old_unused_capacity = currentUnusedCapacity();
    // Given config limits old_unused_capacity should never exceed int32_t.
    // TODO(alyssawilk) move remaining_streams_, concurrent_stream_limit_ and
    // currentUnusedCapacity() to be explicit int32_t
    ASSERT(std::numeric_limits<int32_t>::max() >= old_unused_capacity);
    concurrent_stream_limit_ = settings.maxConcurrentStreams().value();
    int64_t delta = old_unused_capacity - currentUnusedCapacity();
    parent_.decrClusterStreamCapacity(delta);
    ENVOY_CONN_LOG(trace, "Decreasing stream capacity by {}", *codec_client_, delta);
    negative_capacity_ += delta;
  }
  // As we don't increase stream limits when maxConcurrentStreams goes up, treat
  // a stream limit of 0 as a GOAWAY.
  if (concurrent_stream_limit_ == 0) {
    parent_.transitionActiveClientState(*this, ActiveClient::State::DRAINING);
  }
}

void MultiplexedActiveClientBase::onStreamDestroy() {
  parent().onStreamClosed(*this, false);

  // If we are destroying this stream because of a disconnect, do not check for drain here. We will
  // wait until the connection has been fully drained of streams and then check in the connection
  // event callback.
  if (!closed_with_active_rq_) {
    parent().checkForIdleAndCloseIdleConnsIfDraining();
  }
}

void MultiplexedActiveClientBase::onStreamReset(Http::StreamResetReason reason) {
  switch (reason) {
  case StreamResetReason::ConnectionTermination:
  case StreamResetReason::ConnectionFailure:
    parent_.host()->cluster().stats().upstream_rq_pending_failure_eject_.inc();
    closed_with_active_rq_ = true;
    break;
  case StreamResetReason::LocalReset:
  case StreamResetReason::ProtocolError:
  case StreamResetReason::OverloadManager:
    parent_.host()->cluster().stats().upstream_rq_tx_reset_.inc();
    break;
  case StreamResetReason::RemoteReset:
    parent_.host()->cluster().stats().upstream_rq_rx_reset_.inc();
    break;
  case StreamResetReason::LocalRefusedStreamReset:
  case StreamResetReason::RemoteRefusedStreamReset:
  case StreamResetReason::Overflow:
  case StreamResetReason::ConnectError:
    break;
  }
}

uint64_t maxStreamsPerConnection(uint64_t max_streams_config) {
  return (max_streams_config != 0) ? max_streams_config : DEFAULT_MAX_STREAMS;
}

MultiplexedActiveClientBase::MultiplexedActiveClientBase(HttpConnPoolImplBase& parent,
                                                         uint32_t max_concurrent_streams,
                                                         Stats::Counter& cx_total)
    : Envoy::Http::ActiveClient(
          parent, maxStreamsPerConnection(parent.host()->cluster().maxRequestsPerConnection()),
          max_concurrent_streams) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);
  cx_total.inc();
}

MultiplexedActiveClientBase::MultiplexedActiveClientBase(HttpConnPoolImplBase& parent,
                                                         uint32_t max_concurrent_streams,
                                                         Stats::Counter& cx_total,
                                                         Upstream::Host::CreateConnectionData& data)
    : Envoy::Http::ActiveClient(
          parent, maxStreamsPerConnection(parent.host()->cluster().maxRequestsPerConnection()),
          max_concurrent_streams, data) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);
  cx_total.inc();
}

MultiplexedActiveClientBase::MultiplexedActiveClientBase(Envoy::Http::HttpConnPoolImplBase& parent,
                                                         Upstream::Host::CreateConnectionData& data,
                                                         uint32_t max_concurrent_streams,
                                                         Stats::Counter& cx_total)
    : Envoy::Http::ActiveClient(
          parent, maxStreamsPerConnection(parent.host()->cluster().maxRequestsPerConnection()),
          max_concurrent_streams, data) {
  codec_client_->setCodecClientCallbacks(*this);
  codec_client_->setCodecConnectionCallbacks(*this);
  cx_total.inc();
}

bool MultiplexedActiveClientBase::closingWithIncompleteStream() const {
  return closed_with_active_rq_;
}

RequestEncoder& MultiplexedActiveClientBase::newStreamEncoder(ResponseDecoder& response_decoder) {
  return codec_client_->newStream(response_decoder);
}

} // namespace Http
} // namespace Envoy
