#include "common/http/http2/conn_pool.h"

#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/upstream/upstream.h"

#include "common/http/http2/codec_impl.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Http {
namespace Http2 {

using Upstream::ConnectionRequestPolicy;

ConnPoolImpl::ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                           Upstream::ResourcePriority priority,
                           const Network::ConnectionSocket::OptionsSharedPtr& options)
    : ConnPoolImplBase(std::move(host), std::move(priority)), dispatcher_(dispatcher),
      socket_options_(options) {}

void ConnPoolImpl::applyToEachClient(std::list<ActiveClientPtr>& client_list,
                                     const std::function<void(const ActiveClientPtr&)>& fn) {
  for (auto it = client_list.begin(); it != client_list.end();) {
    auto const& client = *it++;
    fn(client);
  }
}

ConnPoolImpl::~ConnPoolImpl() {
  drainConnections();
  dispatcher_.clearDeferredDeleteList();
}

void ConnPoolImpl::ConnPoolImpl::drainConnections() {
  applyToEachClient(connecting_clients_, [this](const ActiveClientPtr& client) {
    client->state_ = ConnectionRequestPolicy::State::Drain;
    client->moveBetweenLists(connecting_clients_, drain_clients_);
  });

  applyToEachClient(ready_clients_, [this](const ActiveClientPtr& client) {
    client->state_ = ConnectionRequestPolicy::State::Drain;
    client->moveBetweenLists(ready_clients_, drain_clients_);
  });

  applyToEachClient(overflow_clients_, [this](const ActiveClientPtr& client) {
    client->state_ = ConnectionRequestPolicy::State::Drain;
    client->moveBetweenLists(overflow_clients_, drain_clients_);
  });

  applyToEachClient(busy_clients_, [this](const ActiveClientPtr& client) {
    client->state_ = ConnectionRequestPolicy::State::Drain;
    client->moveBetweenLists(busy_clients_, drain_clients_);
  });

  checkForDrained();
}

void ConnPoolImpl::addDrainedCallback(DrainedCb cb) {
  drained_callbacks_.push_back(cb);
  checkForDrained();
}

Upstream::ResourceManager& ConnPoolImpl::ActiveClient::resourceManager() const {
  return parent_.host()->cluster().resourceManager(parent_.resourcePriority());
}

bool ConnPoolImpl::hasActiveConnections() const {
  return !pending_requests_.empty() || !busy_clients_.empty() || !overflow_clients_.empty() ||
         !drain_clients_.empty();
}

void ConnPoolImpl::checkForDrained() {
  for (auto it = drain_clients_.begin(); it != drain_clients_.end();) {
    auto& client = *it++;
    if (client->client_ && client->client_->numActiveRequests() == 0 &&
        !client->client_->remoteClosed()) {
      ENVOY_CONN_LOG(debug, "adding to close list", *client->client_);
      client->moveBetweenLists(drain_clients_, to_close_clients_);
    }
  }

  while (!to_close_clients_.empty()) {
    auto& client = to_close_clients_.front();
    ENVOY_CONN_LOG(debug, "closing from drained list", *client->client_);
    client->client_->close();
  }

  if (!drained_callbacks_.empty() && pending_requests_.empty() && busy_clients_.empty() &&
      overflow_clients_.empty()) {
    ENVOY_LOG(debug, "draining ready clients");

    while (!ready_clients_.empty()) {
      ready_clients_.front()->client_->close();
    }

    for (const DrainedCb& cb : drained_callbacks_) {
      cb();
    }
  }
}

void ConnPoolImpl::attachRequestToClient(ConnPoolImpl::ActiveClient& client,
                                         StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) {
  ENVOY_CONN_LOG(debug, "attaching request to connection", *client.client_);
  client.total_streams_++;

  if (client.state_ == ConnectionRequestPolicy::State::Ready) {
    client.idle_timer_->disableTimer();
    client.idle_timer_.reset();
  }

  host_->stats().rq_total_.inc();
  host_->stats().rq_active_.inc();
  host_->cluster().stats().upstream_rq_total_.inc();
  host_->cluster().stats().upstream_rq_active_.inc();
  host_->cluster().resourceManager(priority_).requests().inc();
  callbacks.onPoolReady(client.client_->newStream(response_decoder), client.real_host_description_);
}

void ConnPoolImpl::createNewConnection() {
  ENVOY_LOG(debug, "creating a new connection");
  ActiveClientPtr client(new ActiveClient(*this));
  client->state_ = ConnectionRequestPolicy::State::Init;
  ENVOY_CONN_LOG(debug, "Moving new connection to connecting_clients list", *client->client_);
  client->moveIntoList(std::move(client), connecting_clients_);
}

ConnectionPool::Cancellable* ConnPoolImpl::newStream(Http::StreamDecoder& response_decoder,
                                                     ConnectionPool::Callbacks& callbacks) {
  ENVOY_LOG(debug, "new stream");
  std::list<ActiveClientPtr>* client_list = nullptr;

  if (!busy_clients_.empty()) {
    client_list = &busy_clients_;
  } else if (!ready_clients_.empty()) {
    client_list = &ready_clients_;
  }

  if (client_list) {
    auto& client = client_list->front();

    ENVOY_CONN_LOG(debug, "using existing connection", *client->client_);
    attachRequestToClient(*client, response_decoder, callbacks);

    ENVOY_LOG(debug, "max requests per connection for cluster {}: {}", host_->cluster().name(),
              host_->cluster().maxRequestsPerConnection());

    const auto state = host_->cluster().connectionPolicy().onNewStream(*client);
    switch (state) {
    case ConnectionRequestPolicy::State::Active:
      if (client->state_ != ConnectionRequestPolicy::State::Active) {
        ENVOY_CONN_LOG(debug, "moving to active list after attaching request", *client->client_);
        client->moveBetweenLists(*client_list, busy_clients_);
      }
      break;
    case ConnectionRequestPolicy::State::Overflow:
      if (client->state_ != ConnectionRequestPolicy::State::Overflow) {
        ENVOY_CONN_LOG(debug, "moving to overflow list after attaching request", *client->client_);
        client->moveBetweenLists(*client_list, overflow_clients_);
      }
      break;
    case ConnectionRequestPolicy::State::Drain:
      if (client->state_ != ConnectionRequestPolicy::State::Drain) {
        ENVOY_CONN_LOG(debug, "moving to drain list after attaching request", *client->client_);
        client->moveBetweenLists(*client_list, drain_clients_);
      }
      break;
    default:
      ASSERT(false);
    }

    client->state_ = state;

    return nullptr;
  }

  if (!host_->cluster().resourceManager(priority_).pendingRequests().canCreate()) {
    ENVOY_LOG(debug, "max pending requests overflow");
    callbacks.onPoolFailure(ConnectionPool::PoolFailureReason::Overflow, absl::string_view(),
                            nullptr);
    host_->cluster().stats().upstream_rq_pending_overflow_.inc();
    return nullptr;
  }

  // If we have no connections at all, make one no matter what so we don't starve.
  if ((ready_clients_.empty() && busy_clients_.empty())) {
    ENVOY_LOG(debug, "no ready or busy clients available for new stream");
    createNewConnection();
  }

  ENVOY_LOG(debug, "queueing request as no connection available");
  return newPendingRequest(response_decoder, callbacks);
}

void ConnPoolImpl::onConnectionEvent(ActiveClient& client, Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    ENVOY_CONN_LOG(debug, "client disconnected", *client.client_);

    ENVOY_CONN_LOG(debug, "Got close event , remote: {}", *client.client_,
                   event == Network::ConnectionEvent::RemoteClose);
    Envoy::Upstream::reportUpstreamCxDestroy(host_, event);
    if (client.closed_with_active_rq_) {
      Envoy::Upstream::reportUpstreamCxDestroyActiveRequest(host_, event);
    }

    if (event == Network::ConnectionEvent::RemoteClose) {
      client.remote_closed_ = true;
    }

    ActiveClientPtr removed;
    bool check_for_drained = true;

    if (!client.connect_timer_) {
      // if not in ready list
      switch (client.state_) {
      case ConnectionRequestPolicy::State::Overflow: {
        removed = client.removeFromList(overflow_clients_);
        break;
      }
      case ConnectionRequestPolicy::State::Drain: {
        if (!client.remote_closed_) {
          ENVOY_CONN_LOG(debug, "client removed from drain list", *client.client_);
          removed = client.removeFromList(to_close_clients_);
        }

        break;
      }
      case ConnectionRequestPolicy::State::Active: {
        ENVOY_CONN_LOG(debug, "client removed from busy list", *client.client_);
        removed = client.removeFromList(busy_clients_);
        break;
      }
      default:
        ENVOY_CONN_LOG(debug, "client removed from busy list", *client.client_);
        removed = client.removeFromList(busy_clients_);
        break;
      }
    } else {
      // in ready list
      host_->cluster().stats().upstream_cx_connect_fail_.inc();
      host_->stats().cx_connect_fail_.inc();

      ENVOY_CONN_LOG(debug, "client removed from connecting list", *client.client_);
      removed = client.removeFromList(connecting_clients_);
      check_for_drained = false;

      // Raw connect failures should never happen under normal circumstances. If we have an upstream
      // that is behaving badly, requests can get stuck here in the pending state. If we see a
      // connect failure, we purge all pending requests so that calling code can determine what to
      // do with the request.
      purgePendingRequests(client.real_host_description_,
                           client.client_->connectionFailureReason());
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

  if (event == Network::ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "Got connected event", *client.client_);
    conn_connect_ms_->complete();

    client.upstream_ready_ = true;
    onUpstreamReady(client);
  }

  if (client.connect_timer_) {
    client.connect_timer_->disableTimer();
    client.connect_timer_.reset();
  }
}

void ConnPoolImpl::onIdleTimeout(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "idle timeout", *client.client_);
  client.moveBetweenLists(ready_clients_, drain_clients_);
  client.state_ = ConnectionRequestPolicy::State::Drain;
  checkForDrained();
}

void ConnPoolImpl::onConnectTimeout(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "connect timeout", *client.client_);
  host_->cluster().stats().upstream_cx_connect_timeout_.inc();

  if (client.state_ == ConnectionRequestPolicy::State::Active) {
    client.moveBetweenLists(busy_clients_, drain_clients_);
  }
  if (client.state_ == ConnectionRequestPolicy::State::Init) {
    client.moveBetweenLists(connecting_clients_, drain_clients_);
  }

  client.state_ = ConnectionRequestPolicy::State::Drain;

  client.connect_timer_->disableTimer();
  client.connect_timer_.reset();

  checkForDrained();
}

void ConnPoolImpl::onGoAway(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "remote goaway", *client.client_);
  host_->cluster().stats().upstream_cx_close_notify_.inc();

  switch (client.state_) {
  case ConnectionRequestPolicy::State::Overflow: {
    client.moveBetweenLists(overflow_clients_, drain_clients_);
    break;
  }
  case ConnectionRequestPolicy::State::Active: {
    client.moveBetweenLists(busy_clients_, drain_clients_);
    break;
  }
  default:
    ASSERT(client.state_ == ConnectionRequestPolicy::State::Drain);
    break;
  }

  client.state_ = ConnectionRequestPolicy::State::Drain;
}

void ConnPoolImpl::onStreamDestroy(ActiveClient& client) {
  ENVOY_CONN_LOG(debug, "destroying stream: {} remaining", *client.client_,
                 client.client_->numActiveRequests());
  host_->stats().rq_active_.dec();
  host_->cluster().stats().upstream_rq_active_.dec();
  host_->cluster().resourceManager(priority_).requests().dec();
  client.total_streams_--;

  bool check_drained = false;
  const auto state = host_->cluster().connectionPolicy().onStreamReset(client, client.state_);
  switch (state) {
  case ConnectionRequestPolicy::State::Active: {
    if (client.state_ == ConnectionRequestPolicy::State::Overflow) {
      client.moveBetweenLists(overflow_clients_, busy_clients_);
    }
    if (client.state_ == ConnectionRequestPolicy::State::Drain) {
      client.moveBetweenLists(drain_clients_, busy_clients_);
    }

    break;
  }
  case ConnectionRequestPolicy::State::Drain: {
    if (client.state_ == ConnectionRequestPolicy::State::Overflow) {
      ENVOY_CONN_LOG(debug, "moving to drain list", *client.client_);
      check_drained = true;
      client.moveBetweenLists(overflow_clients_, drain_clients_);
    }

    if (client.state_ == ConnectionRequestPolicy::State::Active) {
      ENVOY_CONN_LOG(debug, "moving to drain list", *client.client_);
      check_drained = true;
      client.moveBetweenLists(overflow_clients_, drain_clients_);
    }

    if (client.state_ == ConnectionRequestPolicy::State::Drain) {
      ENVOY_CONN_LOG(debug, "moving to drain list", *client.client_);
      check_drained = true;
    }
    break;
  }
  default:
    ASSERT(false);
    break;
  }

  client.state_ = state;

  if (check_drained) {
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

void ConnPoolImpl::onUpstreamReady(ActiveClient& client) {
  if (client.state_ == ConnectionRequestPolicy::State::Init) {
    if (pending_requests_.empty()) {
      // There is nothing to service or delayed processing is requested, so just move the connection
      // into the ready list.
      ENVOY_CONN_LOG(debug, "moving to ready", *client.client_);
      client.moveBetweenLists(connecting_clients_, ready_clients_);
      client.state_ = ConnectionRequestPolicy::State::Ready;
      client.idle_timer_->enableTimer(std::chrono::milliseconds(500));
    } else {
      client.moveBetweenLists(connecting_clients_, busy_clients_);
      client.state_ = ConnectionRequestPolicy::State::Active;

      // There is work to do immediately so bind a request to the client and move it to the busy
      // list. Pending requests are pushed onto the front, so pull from the back.
      ENVOY_CONN_LOG(debug, "attaching to next request", *client.client_);
      attachRequestToClient(client, pending_requests_.back()->decoder_,
                            pending_requests_.back()->callbacks_);
      pending_requests_.pop_back();

      const auto state = host_->cluster().connectionPolicy().onNewStream(client);
      switch (state) {
      case ConnectionRequestPolicy::State::Active:
        ENVOY_CONN_LOG(debug, "is active", *client.client_);
        break;
      case ConnectionRequestPolicy::State::Overflow:
        ENVOY_CONN_LOG(debug, "moving to overflow list after attaching request", *client.client_);
        client.moveBetweenLists(busy_clients_, overflow_clients_);
        break;
      case ConnectionRequestPolicy::State::Drain:
        ENVOY_CONN_LOG(debug, "moving to drain list after attaching request", *client.client_);
        client.moveBetweenLists(busy_clients_, drain_clients_);
        break;
      default:
        ENVOY_CONN_LOG(warn, "in unexpected state: {}", *client.client_, static_cast<int>(state));
        ASSERT(false);
      }

      client.state_ = state;
    }
  }

  if (ready_clients_.empty() && busy_clients_.empty() && connecting_clients_.empty()) {
    ENVOY_LOG(debug, "creating new connection as no exiting ready or busy clients");
    createNewConnection();
  }

  ENVOY_LOG(debug, "handled onUpsreamReady");
}

ConnPoolImpl::ActiveClient::ActiveClient(ConnPoolImpl& parent)
    : parent_(parent),
      connect_timer_(parent_.dispatcher_.createTimer([this]() -> void { onConnectTimeout(); })),
      idle_timer_(parent_.dispatcher_.createTimer([this]() -> void { onIdleTimeout(); })) {
  parent_.conn_connect_ms_ = std::make_unique<Stats::Timespan>(
      parent_.host_->cluster().stats().upstream_cx_connect_ms_, parent_.dispatcher_.timeSource());
  Upstream::Host::CreateConnectionData data =
      parent_.host_->createConnection(parent_.dispatcher_, parent_.socket_options_, nullptr);
  real_host_description_ = data.host_description_;
  client_ = parent_.createCodecClient(data);
  client_->addConnectionCallbacks(*this);
  client_->setCodecClientCallbacks(*this);
  client_->setCodecConnectionCallbacks(*this);
  connect_timer_->enableTimer(parent_.host_->cluster().connectTimeout());

  parent_.host_->stats().cx_total_.inc();
  parent_.host_->stats().cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_total_.inc();
  parent_.host_->cluster().stats().upstream_cx_active_.inc();
  parent_.host_->cluster().stats().upstream_cx_http2_total_.inc();
  conn_length_ = std::make_unique<Stats::Timespan>(
      parent_.host_->cluster().stats().upstream_cx_length_ms_, parent_.dispatcher_.timeSource());

  client_->setConnectionStats({parent_.host_->cluster().stats().upstream_cx_rx_bytes_total_,
                               parent_.host_->cluster().stats().upstream_cx_rx_bytes_buffered_,
                               parent_.host_->cluster().stats().upstream_cx_tx_bytes_total_,
                               parent_.host_->cluster().stats().upstream_cx_tx_bytes_buffered_,
                               &parent_.host_->cluster().stats().bind_errors_, nullptr});
}

ConnPoolImpl::ActiveClient::~ActiveClient() {
  parent_.host_->stats().cx_active_.dec();
  parent_.host_->cluster().stats().upstream_cx_active_.dec();
  conn_length_->complete();
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
