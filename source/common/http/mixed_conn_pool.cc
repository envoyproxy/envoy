#include "source/common/http/mixed_conn_pool.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/http1/conn_pool.h"
#include "source/common/http/http2/conn_pool.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tcp/conn_pool.h"

namespace Envoy {
namespace Http {

Envoy::ConnectionPool::ActiveClientPtr HttpConnPoolImplMixed::instantiateActiveClient() {
  uint32_t initial_streams = 1;
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.allow_concurrency_for_alpn_pool")) {
    initial_streams = Http2::ActiveClient::calculateInitialStreamsLimit(
        http_server_properties_cache_, origin_, host());
  }
  return std::make_unique<Tcp::ActiveTcpClient>(
      *this, Envoy::ConnectionPool::ConnPoolImplBase::host(), initial_streams);
}

CodecClientPtr
HttpConnPoolImplMixed::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  auto protocol = protocol_ == Protocol::Http11 ? CodecType::HTTP1 : CodecType::HTTP2;
  CodecClientPtr codec{new CodecClientProd(protocol, std::move(data.connection_),
                                           data.host_description_, dispatcher_, random_generator_,
                                           transportSocketOptions())};
  return codec;
}

void HttpConnPoolImplMixed::onConnected(Envoy::ConnectionPool::ActiveClient& client) {
  // onConnected is called under the stack of the Network::Connection raising
  // the Connected event. The first time it is called, it's called for a TCP
  // client, the TCP client is detached from the connection and discarded, and an
  // HTTP client is associated with that connection. When the first call returns, the
  // Network::Connection will inform the new callback (the HTTP client) that it
  // is connected. The early return is to ignore that second call.
  if (client.protocol() != absl::nullopt) {
    return;
  }

  // If an old TLS stack does not negotiate alpn, it likely does not support
  // HTTP/2. Fail over to HTTP/1.
  protocol_ = Protocol::Http11;
  ASSERT(dynamic_cast<Tcp::ActiveTcpClient*>(&client) != nullptr);
  auto tcp_client = static_cast<Tcp::ActiveTcpClient*>(&client);
  std::string alpn = tcp_client->connection_->nextProtocol();
  if (!alpn.empty()) {
    if (alpn == Http::Utility::AlpnNames::get().Http11) {
      protocol_ = Http::Protocol::Http11;
    } else if (alpn == Http::Utility::AlpnNames::get().Http2) {
      protocol_ = Http::Protocol::Http2;
    }
  }

  uint32_t old_effective_limit = client.effectiveConcurrentStreamLimit();
  if (protocol_ == Http::Protocol::Http11 && client.concurrent_stream_limit_ != 1) {
    // The estimates were all based on assuming HTTP/2 would be negotiated. Adjust down.
    uint32_t delta = client.concurrent_stream_limit_ - 1;
    client.concurrent_stream_limit_ = 1;
    decrConnectingAndConnectedStreamCapacity(delta, client);
    if (http_server_properties_cache_ && origin_.has_value()) {
      http_server_properties_cache_->setConcurrentStreams(origin_.value(), 1);
    }
  }

  Upstream::Host::CreateConnectionData data{std::move(tcp_client->connection_),
                                            client.real_host_description_};
  // As this connection comes from the tcp connection pool, it will be
  // read-disabled to handle TCP traffic where upstream sends data first. Undo
  // this as it is not necessary for HTTP/HTTPS.
  data.connection_->readDisable(false);
  data.connection_->removeConnectionCallbacks(*tcp_client);
  data.connection_->removeReadFilter(tcp_client->read_filter_handle_);
  dispatcher_.deferredDelete(client.removeFromList(owningList(client.state())));

  std::unique_ptr<ActiveClient> new_client;
  if (protocol_ == Http::Protocol::Http11) {
    new_client = std::make_unique<Http1::ActiveClient>(*this, data);
  } else {
    new_client = std::make_unique<Http2::ActiveClient>(*this, data);
  }
  // When we switch from TCP to HTTP clients, the base class onConnectionEvent
  // will be called for both, so add to the connecting stream capacity to
  // balance it being decremented.
  connecting_stream_capacity_ += new_client->effectiveConcurrentStreamLimit();
  // The global capacity is not adjusted in onConnectionEvent, so simply update
  // it to reflect any difference between the TCP stream limits and HTTP/2
  // stream limits.
  if (new_client->effectiveConcurrentStreamLimit() > old_effective_limit) {
    state_.incrConnectingAndConnectedStreamCapacity(new_client->effectiveConcurrentStreamLimit() -
                                                    old_effective_limit);
  }
  new_client->setState(ActiveClient::State::Connecting);
  LinkedList::moveIntoList(std::move(new_client), owningList(new_client->state()));
}

} // namespace Http
} // namespace Envoy
