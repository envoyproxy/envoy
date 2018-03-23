#include "common/http/http1/conn_pool.h"

#include <cstdint>
#include <list>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/http/codec_client.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {
namespace Http1 {

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options)
    : dispatcher_(dispatcher), host_(host), priority_(priority), socket_options_(options),
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

void ConnPoolImpl::attachRequestToClient(ActiveClient& client, StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) {
  ASSERT(!client.stream_wrapper_);
  client.stream_wrapper_.reset(new StreamWrapper(response_decoder, client));
  callbacks.onPoolReady(*client.stream_wrapper_, client.real_host_description_);
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

ConnectionPool::Cancellable* ConnPoolImpl::newStream(StreamDecoder& response_decoder,
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
    if ((ready_clients_.size() == 0 && busy_clients_.size() == 0) || can_create_connection) {
      createNewConnection();
    }

    ENVOY_LOG(debug, "queueing request due to no available connections");
    PendingRequestPtr pending_request(new PendingRequest(*this, response_decoder, callbacks));
    pending_request->moveIntoList(std::move(pending_request), pending_requests_);
    return pending_requests_.front().get();
  } else {
    ENVOY_LOG(debug, "max pending requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }
}

void ConnPoolImpl::onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    // The client died.
    ENVOY_CONN_LOG(debug, "client disconnected", *client.codec_client_);
    ActiveClientPtr removed;
    bool check_for_drained = true;
    if (client.stream_wrapper_) {
      if (!client.stream_wrapper_->decode_complete_) {
        if (event == Network::ConnectionEvent::LocalClose) {
          host_->cluster().stats().upstream_cx_destroy_local_with_active_rq_.inc();
        }
        if (event == Network::ConnectionEvent::RemoteClose) {
          host_->cluster().stats().upstream_cx_destroy_remote_with_active_rq_.inc();
        }
        host_->cluster().stats().upstream_cx_destroy_with_active_rq_.inc();
      }

      // There is an active request attached to this client. The underlying codec client will
      // already have "reset" the stream to fire the reset callback. All we do here is just
      // destroy the client.
      removed = client.removeFromList(busy_clients_);
    } else if (!client.connect_timer_) {
      // The connect timer is destroyed on connect. The lack of a connect timer means that this
      // client is idle and in the ready pool.
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
      // NOTE: We move the existing pending requests to a temporary list. This is done so that
      //       if retry logic submits a new request to the pool, we don't fail it inline.
      std::list<PendingRequestPtr> pending_requests_to_purge(std::move(pending_requests_));
      while (!pending_requests_to_purge.empty()) {
        PendingRequestPtr request =
            pending_requests_to_purge.front()->removeFromList(pending_requests_to_purge);
        host_->cluster().stats().upstream_rq_pending_failure_eject_.inc();
        request->callbacks_.onPoolFailure(ConnectionPool::PoolFailureReason::ConnectionFailure,
                                          client.real_host_description_);
      }
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

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }

  // Note that the order in this function is important. Concretely, we must destroy the connect
  // timer before we process a connected idle client, because if this results in an immediate
  // drain/destruction event, we key off of the existence of the connect timer above to determine
  // whether the client is in the ready list (connected) or the busy list (failed to connect).
  if (event == Network::ConnectionEvent::Connected) {
    conn_connect_ms_->complete();
    processIdleClient(client);
  }
}

void ConnPoolImpl::onDownstreamReset(ActiveClient& client) {
  // If we get a downstream reset to an attached client, we just blow it away.
  client.codec_client_->close();
}

void ConnPoolImpl::onPendingRequestCancel(PendingRequest& request) {
  ENVOY_LOG(debug, "cancelling pending request");
  request.removeFromList(pending_requests_);
  host_->cluster().stats().upstream_rq_cancelled_.inc();
  checkForDrained();
}

void ConnPoolImpl::onResponseComplete(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "response complete", *client.codec_client_);
  if (!client.stream_wrapper_->encode_complete_) {
    ENVOY_CONN_LOG(debug, "response before request complete", *client.codec_client_);
    onDownstreamReset(client);
  } else if (client.stream_wrapper_->saw_close_header_ || client.codec_client_->remoteClosed()) {
    ENVOY_CONN_LOG(debug, "saw upstream connection: close", *client.codec_client_);
    onDownstreamReset(client);
  } else if (client.remaining_requests_ > 0 && --client.remaining_requests_ == 0) {
    ENVOY_CONN_LOG(debug, "maximum requests per connection", *client.codec_client_);
    host_->cluster().stats().upstream_cx_max_requests_.inc();
    onDownstreamReset(client);
  } else {
    processIdleClient(client);
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

void ConnPoolImpl::processIdleClient(ActiveClient& client) {
  client.stream_wrapper_.reset();
  // There is nothing to service so just move the connection into the ready list.
  ENVOY_CONN_LOG(debug, "moving to ready", *client.codec_client_);
  client.moveBetweenLists(busy_clients_, ready_clients_);

  if (!pending_requests_.empty() && !upstream_ready_enabled_) {
    upstream_ready_enabled_ = true;
    upstream_ready_timer_->enableTimer(std::chrono::milliseconds(0));
  }

  checkForDrained();
}

ConnPoolImpl::StreamWrapper::StreamWrapper(StreamDecoder& response_decoder, ActiveClient& parent)
    : StreamEncoderWrapper(parent.codec_client_->newStream(*this)),
      StreamDecoderWrapper(response_decoder), parent_(parent) {

  StreamEncoderWrapper::inner_.getStream().addCallbacks(*this);
  parent_.parent_.host_->cluster().stats().upstream_rq_total_.inc();
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.inc();
  parent_.parent_.host_->stats().rq_total_.inc();
  parent_.parent_.host_->stats().rq_active_.inc();
}

ConnPoolImpl::StreamWrapper::~StreamWrapper() {
  parent_.parent_.host_->cluster().stats().upstream_rq_active_.dec();
  parent_.parent_.host_->stats().rq_active_.dec();
}

void ConnPoolImpl::StreamWrapper::onEncodeComplete() { encode_complete_ = true; }

void ConnPoolImpl::StreamWrapper::decodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  if (headers->Connection() &&
      0 == StringUtil::caseInsensitiveCompare(headers->Connection()->value().c_str(),
                                              Headers::get().ConnectionValues.Close.c_str())) {
    saw_close_header_ = true;
    parent_.parent_.host_->cluster().stats().upstream_cx_close_notify_.inc();
  }

  StreamDecoderWrapper::decodeHeaders(std::move(headers), end_stream);
}

void ConnPoolImpl::StreamWrapper::onDecodeComplete() {
  decode_complete_ = encode_complete_;
  parent_.parent_.onResponseComplete(parent_);
}

ConnPoolImpl::PendingRequest::PendingRequest(ConnPoolImpl& parent, StreamDecoder& decoder,
                                             ConnectionPool::Callbacks& callbacks)
    : parent_(parent), decoder_(decoder), callbacks_(callbacks) {
  parent_.host_->cluster().stats().upstream_rq_pending_total_.inc();
  parent_.host_->cluster().stats().upstream_rq_pending_active_.inc();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().inc();
}

ConnPoolImpl::PendingRequest::~PendingRequest() {
  parent_.host_->cluster().stats().upstream_rq_pending_active_.dec();
  parent_.host_->cluster().resourceManager(parent_.priority_).pendingRequests().dec();
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : parent_(parent),
      connect_timer_(parent_.dispatcher_.createTimer([this]() -> void { onConnectTimeout(); })),
      remaining_requests_(parent_.host_->cluster().maxRequestsPerConnection()) {

  parent_.conn_connect_ms_.reset(
      new Stats::Timespan(parent_.host_->cluster().stats().upstream_cx_connect_ms_));
  Upstream::Host::CreateConnectionData data =
      parent_.host_->createConnection(parent_.dispatcher_, parent_.socket_options_);
  real_host_description_ = data.host_description_;
  codec_client_ = parent_.createCodecClient(data);
  codec_client_->addConnectionCallbacks(*this);

  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http1_total_.inc();
  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  conn_length_.reset(new Stats::Timespan(parent_.host_->cluster().stats().upstream_cx_length_ms_));
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().inc();

  codec_client_->setConnectionStats(
      {parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
       parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
       &parent_.host_->cluster().stats().bind_errors_});
}

ConnPoolImpl::ActiveClient::~ActiveClient() {
  parent_.host_->cluster().stats().upstream_cx_active_.dec();
  parent_.host_->stats().cx_active_.dec();
  conn_length_->complete();
  parent_.host_->cluster().resourceManager(parent_.priority_).connections().dec();
}

void ConnPoolImpl::ActiveClient::onConnectTimeout() {
  // We just close the client at this point. This will result in both a timeout and a connect
  // failure and will fold into all the normal connect failure logic.
  ENVOY_CONN_LOG(debug, "connect timeout", *codec_client_);
  parent_.host_->cluster().stats().upstream_cx_connect_timeout_.inc();
  codec_client_->close();
}

CodecClientPtr ConnPoolImplProd::createCodecClient(Upstream::Host::CreateConnectionData& data) {
  CodecClientPtr codec{new CodecClientProd(CodecClient::Type::HTTP1, std::move(data.connection_),
                                           data.host_description_, dispatcher_)};
  return codec;
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
