#include "common/upstream/upstream_connection_pool_impl.h"

#include "common/common/lock_guard.h"

namespace Envoy {
namespace Upstream {

UpstreamConnectionPoolImpl::UpstreamConnectionPoolImpl(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

UpstreamConnectionPoolImpl::~UpstreamConnectionPoolImpl() {
  for (auto& pool : connection_pools_) {
    auto& clients = pool.second.clients;
    while (!clients.empty()) {
      clients.front()->client_connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }

  // Make sure all clients are destroyed before we are destroyed.
  dispatcher_.clearDeferredDeleteList();
}

void UpstreamConnectionPoolImpl::OfferConnection(HostConstSharedPtr host,
                                                 UpstreamConnectionEssence connection_essence) {
  auto shared_connection_essence =
      std::make_shared<UpstreamConnectionEssence>(std::move(connection_essence));
  dispatcher_.post([this, host, shared_connection_essence]() {
    AcceptConnectionInDispatcherThread(host, std::move(*shared_connection_essence));
  });
  {
    Thread::LockGuard lock(mu_);
    ConnectionPool& pool = connection_pools_[host];
    ++pool.available_connections;
  }
}

bool UpstreamConnectionPoolImpl::RetrieveConnection(
    HostConstSharedPtr host, Event::Dispatcher& dispatcher,
    std::function<void(UpstreamConnectionEssence connection_essence)> accept_cb) {
  {
    Thread::LockGuard lock(mu_);
    ConnectionPool& pool = connection_pools_[host];
    if (pool.available_connections <= 0) {
      return false;
    }
    --pool.available_connections;
  }
  dispatcher_.post([this, host, &dispatcher, accept_cb]() {
    RetrieveConnectionInDispatcherThread(host, dispatcher, accept_cb);
  });
  return true;
}

void UpstreamConnectionPoolImpl::AcceptConnectionInDispatcherThread(
    HostConstSharedPtr host, UpstreamConnectionEssence connection_essence) {
  Thread::LockGuard lock(mu_);
  ConnectionPool& pool = connection_pools_[host];

  auto client_connection = dispatcher_.adoptClientConnection(
      std::move(connection_essence.socket), std::move(connection_essence.transport_socket));
  ActiveClientPtr new_client(new ActiveClient(*this, host, std::move(client_connection),
                                              connection_essence.remaining_requests));
  new_client->moveIntoList(std::move(new_client), pool.clients);
}

void UpstreamConnectionPoolImpl::RetrieveConnectionInDispatcherThread(
    HostConstSharedPtr host, Event::Dispatcher& dispatcher,
    std::function<void(UpstreamConnectionEssence connection_essence)> accept_cb) {
  Thread::LockGuard lock(mu_);
  ConnectionPool& pool = connection_pools_[host];
  if (pool.clients.empty()) {
    // We're over-committed on number of available pooled connections. Release our lease and fail
    // the connection pooling attempt.
    ++pool.available_connections;
    dispatcher.post([accept_cb]() {
      UpstreamConnectionEssence no_connection;
      accept_cb(std::move(no_connection));
    });

    return;
  }
  ActiveClientPtr client = std::move(pool.clients.front());
  pool.clients.pop_front();
  auto shared_connection_essence =
      std::make_shared<UpstreamConnectionEssence>(client->detachSockets());
  client.reset();
  dispatcher.post([accept_cb, shared_connection_essence]() {
    accept_cb(std::move(*shared_connection_essence));
  });
}

void UpstreamConnectionPoolImpl::onEvent(ActiveClient& client, Network::ConnectionEvent /*event*/) {
  {
    ActiveClientPtr removed;
    Thread::LockGuard lock(mu_);
    ConnectionPool& pool = connection_pools_[client.host_];
    --pool.available_connections;
    removed = client.removeFromList(pool.clients);
    dispatcher_.deferredDelete(std::move(removed));
  }
  // TODO Need to call close or other methods?
  // TODO Safe to delete active client and connection with locks held?
}

UpstreamConnectionPoolImpl::ActiveClient::ActiveClient(
    UpstreamConnectionPoolImpl& parent, HostConstSharedPtr host,
    Network::ClientConnectionPtr client_connection, uint64_t remaining_requests)
    : parent_(parent), host_(host), client_connection_(std::move(client_connection)),
      remaining_requests_(remaining_requests) {
  client_connection_->addConnectionCallbacks(*this);

  const absl::optional<std::chrono::milliseconds> idle_timeout = host_->cluster().idleTimeout();
  if (idle_timeout) {
    timeout_timer_ = parent_.dispatcher_.createTimer([this]() -> void { onTimeout(); });
    timeout_timer_->enableTimer(idle_timeout.value());
  }

  client_connection_->addReadFilter(std::make_shared<CloseConnectionReadFilter>(*this));
}

UpstreamConnectionPool::UpstreamConnectionEssence
UpstreamConnectionPoolImpl::ActiveClient::detachSockets() {
  std::pair<Network::ConnectionSocketPtr, Network::TransportSocketPtr> detached =
      client_connection_->detachSockets();
  client_connection_.reset();
  timeout_timer_.reset();
  return {std::move(detached.first), std::move(detached.second), remaining_requests_};
}

void UpstreamConnectionPoolImpl::ActiveClient::closeClientCnnection() {
  timeout_timer_.reset();
  client_connection_->close(Network::ConnectionCloseType::NoFlush);
}

void UpstreamConnectionPoolImpl::ActiveClient::onTimeout() {
  host_->cluster().stats().upstream_cx_idle_timeout_.inc();
  closeClientCnnection();
}

Network::FilterStatus
UpstreamConnectionPoolImpl::CloseConnectionReadFilter::onData(Buffer::Instance& /*data*/,
                                                              bool /*end_stream*/) {
  // TODO more testing. I think code path is triggered by
  // test/integration/integration_test.cc
  // IpVersions/IntegrationTest.AdminDrainDrainsListeners due to stray data.
  active_client_.closeClientCnnection();
  return Network::FilterStatus::Continue;
}

UpstreamConnectionPoolThread::UpstreamConnectionPoolThread(Api::Api& api)
    : api_(api), dispatcher_(api_.allocateDispatcher()) {}

UpstreamConnectionPoolThread::~UpstreamConnectionPoolThread() {
  RELEASE_ASSERT(thread_ == nullptr, "Stop not called");
}

void UpstreamConnectionPoolThread::start() {
  upstream_connection_pool_ = std::make_unique<UpstreamConnectionPoolImpl>(*dispatcher_);
  thread_ = api_.threadFactory().createThread([this]() -> void {
    dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
    upstream_connection_pool_.reset();
  });
  // TODO wait for thread to start?
}

void UpstreamConnectionPoolThread::stop() {
  dispatcher_->exit();
  thread_->join();
  thread_.reset();
}

} // namespace Upstream
} // namespace Envoy
