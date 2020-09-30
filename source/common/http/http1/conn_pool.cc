#include "common/http/http1/conn_pool.h"

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

#include "common/http/codec_client.h"
#include "common/http/codes.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/runtime/runtime_features.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {
namespace Http1 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                           Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : HttpConnPoolImplBase(std::move(host), std::move(priority), dispatcher, options,
                           transport_socket_options, Protocol::Http11),
      upstream_ready_cb_(dispatcher_.createSchedulableCallback([this]() {
        upstream_ready_enabled_ = false;
        onUpstreamReady();
      })),
      random_generator_(random_generator) {}

ConnPoolImpl::~ConnPoolImpl() { destructAllConnections(); }

Envoy::ConnectionPool::ActiveClientPtr ConnPoolImpl::instantiateActiveClient() {
  return std::make_unique<ActiveClient>(*this);
}

void ConnPoolImpl::onDownstreamReset(ActiveClient& client) {
  // If we get a downstream reset to an attached client, we just blow it away.
  client.codec_client_->close();
}

void ConnPoolImpl::onResponseComplete(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "response complete", *client.codec_client_);

  if (!client.stream_wrapper_->encode_complete_) {
    ENVOY_CONN_LOG(debug, "response before request complete", *client.codec_client_);
    onDownstreamReset(client);
  } else if (client.stream_wrapper_->close_connection_ || client.codec_client_->remoteClosed()) {
    ENVOY_CONN_LOG(debug, "saw upstream close connection", *client.codec_client_);
    onDownstreamReset(client);
  } else {
    client.stream_wrapper_.reset();

    if (!pending_streams_.empty() && !upstream_ready_enabled_) {
      upstream_ready_enabled_ = true;
      upstream_ready_cb_->scheduleCallbackCurrentIteration();
    }

    checkForDrained();
  }
}

ConnPoolImpl::StreamWrapper::StreamWrapper(ResponseDecoder& response_decoder, ActiveClient& parent)
    : RequestEncoderWrapper(parent.codec_client_->newStream(*this)),
      ResponseDecoderWrapper(response_decoder), parent_(parent) {
  RequestEncoderWrapper::inner_.getStream().addCallbacks(*this);
}

ConnPoolImpl::StreamWrapper::~StreamWrapper() {
  // Upstream connection might be closed right after response is complete. Setting delay=true
  // here to attach pending requests in next dispatcher loop to handle that case.
  // https://github.com/envoyproxy/envoy/issues/2715
  parent_.parent().onStreamClosed(parent_, true);
}

void ConnPoolImpl::StreamWrapper::onEncodeComplete() { encode_complete_ = true; }

void ConnPoolImpl::StreamWrapper::decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.fixed_connection_close")) {
    close_connection_ =
        HeaderUtility::shouldCloseConnection(parent_.codec_client_->protocol(), *headers);
    if (close_connection_) {
      parent_.parent_.host()->cluster().stats().upstream_cx_close_notify_.inc();
    }
  } else {
    // If Connection: close OR
    //    Http/1.0 and not Connection: keep-alive OR
    //    Proxy-Connection: close
    if ((absl::EqualsIgnoreCase(headers->getConnectionValue(),
                                Headers::get().ConnectionValues.Close)) ||
        (parent_.codec_client_->protocol() == Protocol::Http10 &&
         !absl::EqualsIgnoreCase(headers->getConnectionValue(),
                                 Headers::get().ConnectionValues.KeepAlive)) ||
        (absl::EqualsIgnoreCase(headers->getProxyConnectionValue(),
                                Headers::get().ConnectionValues.Close))) {
      parent_.parent_.host()->cluster().stats().upstream_cx_close_notify_.inc();
      close_connection_ = true;
    }
  }
  ResponseDecoderWrapper::decodeHeaders(std::move(headers), end_stream);
}

void ConnPoolImpl::StreamWrapper::onDecodeComplete() {
  decode_complete_ = encode_complete_;
  parent_.parent().onResponseComplete(parent_);
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : Envoy::Http::ActiveClient(
          parent, parent.host_->cluster().maxRequestsPerConnection(),
          1 // HTTP1 always has a concurrent-request-limit of 1 per connection.
      ) {
  parent.host_->cluster().stats().upstream_cx_http1_total_.inc();
}

bool ConnPoolImpl::ActiveClient::closingWithIncompleteStream() const {
  return (stream_wrapper_ != nullptr) && (!stream_wrapper_->decode_complete_);
}

RequestEncoder& ConnPoolImpl::ActiveClient::newStreamEncoder(ResponseDecoder& response_decoder) {
  ASSERT(!stream_wrapper_);
  stream_wrapper_ = std::make_unique<StreamWrapper>(response_decoder, *this);
  return *stream_wrapper_;
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP1, std::move(data.connection_),
                                           data.host_description_, dispatcher_, random_generator_)};
  return codec;
}

ConnectionPool::InstancePtr
allocateConnPool(Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
                 Upstream::HostConstSharedPtr host, Upstream::ResourcePriority priority,
                 const Network::ConnectionSocket::OptionsSharedPtr& options,
                 const Network::TransportSocketOptionsSharedPtr& transport_socket_options) {
  return std::make_unique<Http::Http1::ProdConnPoolImpl>(
      dispatcher, random_generator, host, priority, options, transport_socket_options);
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
