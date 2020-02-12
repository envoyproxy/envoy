#include "common/http/http1/conn_pool_legacy.h"

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/http/codec_client.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "common/stats/timespan_impl.h"
#include "common/upstream/upstream_impl.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Http {
namespace Legacy {
namespace Http1 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options,
                           const Network::TransportSocketOptionsSharedPtr& transport_socket_options)
    : ConnPoolImplBase(std::move(host), std::move(priority)), dispatcher_(dispatcher),
      socket_options_(options), transport_socket_options_(transport_socket_options),
      upstream_ready_timer_(dispatcher_.createTimer([this]() { onUpstreamReady(); })) {}

ConnPoolImpl::~ConnPoolImpl() {
  while (!ready_clients_.empty()) {
    ready_clients_.front()->codec_client_->close();
  }

  while (!busy_clients_.empty()) {
    busy_clients_.front()->codec_client_->close();
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImpl::drainConnections() {
  while (!ready_clients_.empty()) {
    ready_clients_.front()->codec_client_->close();
  }

  // We drain busy clients by manually setting remaining requests to 1. Thus, when the next
  // response completes the client will be destroyed.
  for (const auto& client : busy_clients_) {
    client->remaining_requests_ = 1;
  }
}

void ConnPoolImpl::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

bool ConnPoolImpl::hasActiveConnections() const {
  return !pending_requests_.empty() || !busy_clients_.empty();
}

void ConnPoolImpl::attachRequestToClient(ActiveClient& client, ResponseDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) {
  ASSERT(!client.stream_wrapper_);
  host_->cluster().stats().upstream_rq_total_.inc();
  host_->stats().rq_total_.inc();
  client.stream_wrapper_ = std::make_unique<StreamWrapper>(response_decoder, client);
  callbacks.onPoolReady(*client.stream_wrapper_, client.real_host_description_,
                        client.codec_client_->streamInfo());
}

void ConnPoolImpl::checkForDrained() {
  if (!drained_callbacks_.empty() && pending_requests_.empty() && busy_clients_.empty()) {
    while (!ready_clients_.empty()) {
      ready_clients_.front()->codec_client_->close();
    }

    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImpl::createNewConnection() {
  ENVOY_LOG(debug, "creating a new connection");
  ActiveClientPtr client(new ActiveClient(*this));
  client->moveIntoList(std::move(client), busy_clients_);
}

ConnectionPool::Cancellable* ConnPoolImpl::newStream(ResponseDecoder& response_decoder,
                                                     ConnectionPool::Callbacks& callbacks) {
  if (!ready_clients_.empty()) {
    ready_clients_.front()->moveBetweenLists(ready_clients_, busy_clients_);
    ENVOY_CONN_LOG(debug, "using existing connection", *busy_clients_.front()->codec_client_);
    attachRequestToClient(*busy_clients_.front(), response_decoder, callbacks);
    return nullptr;
  }

  if (host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    bool can_create_connection =
        host_->cluster().resourceManager(priority_).connections().canCreate();
    if (!can_create_connection) {
      host_->cluster().stats().upstream_cx_overflow_.inc();
    }

    // If we have no connections at all, make one no matter what so we don't starve.
    if ((ready_clients_.empty() && busy_clients_.empty()) || can_create_connection) {
      createNewConnection();
    }

    return newPendingRequest(response_decoder, callbacks);
  } else {
    ENVOY_LOG(debug, "max pending requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }
}

void ConnPoolImpl::onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected, failure reason: {}", *client.codec_client_,
                   client.codec_client_->connectionFailureReason());

    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    ActiveClientPtr removed;
    bool check_for_drained = true;
    if (client.stream_wrapper_) {
      if (!client.stream_wrapper_->decode_complete_) {
        Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
      }

      // There is an active request attached to this client. The underlying codec client will
      // already have "reset" the stream to fire the reset callback. All we do here is just
      // destroy the client.
      removed = client.removeFromList(busy_clients_);
    } else if (client.connectionState() ==
               ConnPoolImplBase::ActiveClient::ConnectionState::Connected) {
      removed = client.removeFromList(ready_clients_);
      check_for_drained = false;
    } else {
      // The only time this happens is if we actually saw a connect failure.
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      removed = client.removeFromList(busy_clients_);

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      ENVOY_CONN_LOG(debug, "purge pending, failure reason: {}", *client.codec_client_,
                     client.codec_client_->connectionFailureReason());
      purgePendingRequests(client.real_host_description_,
                           client.codec_client_->connectionFailureReason());
    }

    dispatcher_.deferredDelete(std::move(removed));

    // If we have pending requests and we just lost a connection we should make a new one.
    if (pending_requests_.size() > (ready_clients_.size() + busy_clients_.size())) {
      createNewConnection();
    }

    if (check_for_drained) {
      checkForDrained();
    }
  }

  client.disarmConnectTimeout();

  // Note that the order in this function is important. Concretely, we must destroy the connect
  // timer before we process a connected idle client, because if this results in an immediate
  // drain/destruction event, we key off of the existence of the connect timer above to determine
  // whether the client is in the ready list (connected) or the busy list (failed to connect).
  if (event == Network::ConnectionEvent::Connected) {
    client.recordConnectionSetup();
    processIdleClient(client, false);
  }
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
  } else if (client.remaining_requests_ > 0 && --client.remaining_requests_ == 0) {
    ENVOY_CONN_LOG(debug, "maximum requests per connection", *client.codec_client_);
    host_->cluster().stats().upstream_cx_max_requests_.inc();
    onDownstreamReset(client);
  } else {
    // Upstream connection might be closed right after response is complete. Setting delay=true
    // here to attach pending requests in next dispatcher loop to handle that case.
    // https://github.com/envoyproxy/envoy/issues/2715
    processIdleClient(client, true);
  }
}

void ConnPoolImpl::onUpstreamReady() {
  upstream_ready_enabled_ = false;
  while (!pending_requests_.empty() && !ready_clients_.empty()) {
    ActiveClient& client = *ready_clients_.front();
    ENVOY_CONN_LOG(debug, "attaching to next request", *client.codec_client_);
    // There is work to do so bind a request to the client and move it to the busy list. Pending
    // requests are pushed onto the front, so pull from the back.
    attachRequestToClient(client, pending_requests_.back()->decoder_,
                          pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
    client.moveBetweenLists(ready_clients_, busy_clients_);
  }
}

void ConnPoolImpl::processIdleClient(ActiveClient& client, bool delay) {
  client.stream_wrapper_.reset();
  if (pending_requests_.empty() || delay) {
    // There is nothing to service or delayed processing is requested, so just move the connection
    // into the ready list.
    ENVOY_CONN_LOG(debug, "moving to ready", *client.codec_client_);
    client.moveBetweenLists(busy_clients_, ready_clients_);
  } else {
    // There is work to do immediately so bind a request to the client and move it to the busy list.
    // Pending requests are pushed onto the front, so pull from the back.
    ENVOY_CONN_LOG(debug, "attaching to next request", *client.codec_client_);
    attachRequestToClient(client, pending_requests_.back()->decoder_,
                          pending_requests_.back()->callbacks_);
    pending_requests_.pop_back();
  }

  if (delay && !pending_requests_.empty() && !upstream_ready_enabled_) {
    upstream_ready_enabled_ = true;
    upstream_ready_timer_->enableTimer(std::chrono::milliseconds(0));
  }

  checkForDrained();
}

ConnPoolImpl::StreamWrapper::StreamWrapper(ResponseDecoder& response_decoder, ActiveClient& parent)
    : RequestEncoderWrapper(parent.codec_client_->newStream(*this)),
      ResponseDecoderWrapper(response_decoder), parent_(parent) {

  RequestEncoderWrapper::inner_.getStream().addCallbacks(*this);
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.inc();
  parent_.parent_.host_->stats().rq_active_.inc();

  // TODO (tonya11en): At the time of writing, there is no way to mix different versions of HTTP
  // traffic in the same cluster, so incrementing the request count in the per-cluster resource
  // manager will not affect circuit breaking in any unexpected ways. Ideally, outstanding requests
  // counts would be tracked the same way in all HTTP versions.
  //
  // See: https://github.com/envoyproxy/envoy/issues/9215
  parent_.parent_.host_->cluster().resourceManager(parent_.parent_.priority_).requests().inc();
}

ConnPoolImpl::StreamWrapper::~StreamWrapper() {
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.dec();
  parent_.parent_.host_->stats().rq_active_.dec();
  parent_.parent_.host_->cluster().resourceManager(parent_.parent_.priority_).requests().dec();
}

void ConnPoolImpl::StreamWrapper::onEncodeComplete() { encode_complete_ = true; }

void ConnPoolImpl::StreamWrapper::decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
  // If Connection: close OR
  //    Http/1.0 and not Connection: keep-alive OR
  //    Proxy-Connection: close
  if ((headers->Connection() &&
       (absl::EqualsIgnoreCase(headers->Connection()->value().getStringView(),
                               Headers::get().ConnectionValues.Close))) ||
      (parent_.codec_client_->protocol() == Protocol::Http10 &&
       (!headers->Connection() ||
        !absl::EqualsIgnoreCase(headers->Connection()->value().getStringView(),
                                Headers::get().ConnectionValues.KeepAlive))) ||
      (headers->ProxyConnection() &&
       (absl::EqualsIgnoreCase(headers->ProxyConnection()->value().getStringView(),
                               Headers::get().ConnectionValues.Close)))) {
    parent_.parent_.host_->cluster().stats().upstream_cx_close_notify_.inc();
    close_connection_ = true;
  }

  ResponseDecoderWrapper::decodeHeaders(std::move(headers), end_stream);
}

void ConnPoolImpl::StreamWrapper::onDecodeComplete() {
  decode_complete_ = encode_complete_;
  parent_.parent_.onResponseComplete(parent_);
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : ConnPoolImplBase::ActiveClient(parent.dispatcher_, parent.host_->cluster()), parent_(parent),
      remaining_requests_(parent_.host_->cluster().maxRequestsPerConnection()) {

  Upstream::Host::CreateConnectionData data = parent_.host_->createConnection(
      parent_.dispatcher_, parent_.socket_options_, parent_.transport_socket_options_);
  real_host_description_ = data.host_description_;
  codec_client_ = parent_.createCodecClient(data);
  codec_client_->addConnectionCallbacks(*this);

  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http1_total_.inc();
  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().inc();

  codec_client_->setConnectionStats(
      {parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &parent_.host_->cluster().stats().bind_errors_, nullptr});
}

ConnPoolImpl::ActiveClient::~ActiveClient() {
  parent_.host_->cluster().stats().upstream_cx_active_.dec();
  parent_.host_->stats().cx_active_.dec();
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().dec();
}

void ConnPoolImpl::ActiveClient::onConnectTimeout() {
  // We just close the client at this point. This will result in both a timeout and a connect
  // failure and will fold into all the normal connect failure logic.
  ENVOY_CONN_LOG(debug, "connect timeout", *codec_client_);
  parent_.host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  codec_client_->close();
}

CodecClientPtr ProdConnPoolImpl::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP1, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

} // namespace Http1
} // namespace Legacy
} // namespace Http
} // namespace Envoy
