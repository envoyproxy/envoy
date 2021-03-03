#include "common/http/conn_pool_base.h"

#include "common/common/assert.h"
#include "common/http/utility.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/runtime/runtime_features.h"
#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {

Network::TransportSocketOptionsSharedPtr
wrapTransportSocketOptions(Network::TransportSocketOptionsSharedPtr transport_socket_options,
                           std::vector<Protocol> protocols) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_default_alpn")) {
    return transport_socket_options;
  }

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
      // TODO(#14829) hard-code H3 ALPN, consider failing if other things are negotiated.
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
    const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
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

} // namespace Http
} // namespace Envoy
