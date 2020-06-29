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
                           Protocol protocol) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http_default_alpn")) {
    return transport_socket_options;
  }

  // If configured to do so, we override the ALPN to use for the upstream connection to match the
  // selected protocol.
  std::string alpn;
  switch (protocol) {
  case Http::Protocol::Http10:
    NOT_REACHED_GCOVR_EXCL_LINE;
  case Http::Protocol::Http11:
    alpn = Http::Utility::AlpnNames::get().Http11;
    break;
  case Http::Protocol::Http2:
    alpn = Http::Utility::AlpnNames::get().Http2;
    break;
  case Http::Protocol::Http3:
    // TODO(snowp): Add once HTTP/3 upstream support is added.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    break;
  }

  if (transport_socket_options) {
    return std::make_shared<Network::AlpnDecoratingTransportSocketOptions>(
        std::move(alpn), transport_socket_options);
  } else {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        "", std::vector<std::string>{}, std::vector<std::string>{}, std::move(alpn));
  }
}

HttpConnPoolImplBase::HttpConnPoolImplBase(
    Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsSharedPtr& transport_socket_options,
    Http::Protocol protocol)
    : Envoy::ConnectionPool::ConnPoolImplBase(
          host, priority, dispatcher, options,
          wrapTransportSocketOptions(transport_socket_options, protocol)) {}

ConnectionPool::Cancellable*
HttpConnPoolImplBase::newStream(Http::ResponseDecoder& response_decoder,
                                Http::ConnectionPool::Callbacks& callbacks) {
  HttpAttachContext context({&response_decoder, &callbacks});
  return Envoy::ConnectionPool::ConnPoolImplBase::newStream(context);
}

bool HttpConnPoolImplBase::hasActiveConnections() const {
  return (!pending_requests_.empty() || (num_active_requests_ > 0));
}

ConnectionPool::Cancellable*
HttpConnPoolImplBase::newPendingRequest(Envoy::ConnectionPool::AttachContext& context) {
  Http::ResponseDecoder& decoder = *typedContext<HttpAttachContext>(context).decoder_;
  Http::ConnectionPool::Callbacks& callbacks = *typedContext<HttpAttachContext>(context).callbacks_;
  ENVOY_LOG(debug, "queueing request due to no available connections");
  Envoy::ConnectionPool::PendingRequestPtr pending_request(
      new HttpPendingRequest(*this, decoder, callbacks));
  pending_request->moveIntoList(std::move(pending_request), pending_requests_);
  return pending_requests_.front().get();
}

void HttpConnPoolImplBase::onPoolReady(Envoy::ConnectionPool::ActiveClient& client,
                                       Envoy::ConnectionPool::AttachContext& context) {
  ActiveClient* http_client = static_cast<ActiveClient*>(&client);
  auto& http_context = typedContext<HttpAttachContext>(context);
  Http::ResponseDecoder& response_decoder = *http_context.decoder_;
  Http::ConnectionPool::Callbacks& callbacks = *http_context.callbacks_;
  Http::RequestEncoder& new_encoder = http_client->newStreamEncoder(response_decoder);
  callbacks.onPoolReady(new_encoder, client.real_host_description_,
                        http_client->codec_client_->streamInfo());
}

} // namespace Http
} // namespace Envoy
