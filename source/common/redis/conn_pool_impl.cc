#include "conn_pool_impl.h"

#include "common/common/assert.h"

namespace Redis {
namespace ConnPool {

ClientPtr ClientImpl::create(const std::string& cluster_name, Upstream::ClusterManager& cm,
                             EncoderPtr&& encoder, DecoderFactory& decoder_factory) {

  Upstream::Host::CreateConnectionData data = cm.tcpConnForCluster(cluster_name);
  if (!data.connection_) {
    return nullptr;
  }

  std::unique_ptr<ClientImpl> client(new ClientImpl(std::move(encoder), decoder_factory));
  client->connection_ = std::move(data.connection_);
  client->connection_->addConnectionCallbacks(*client);
  client->connection_->addReadFilter(Network::ReadFilterPtr{new UpstreamReadFilter(*client)});
  client->connection_->connect();
  client->connection_->noDelay(true);
  return std::move(client);
}

ClientImpl::~ClientImpl() {
  ASSERT(pending_requests_.empty());
  ASSERT(connection_->state() == Network::Connection::State::Closed);
}

void ClientImpl::close() { connection_->close(Network::ConnectionCloseType::NoFlush); }

ActiveRequest* ClientImpl::makeRequest(const RespValue& request,
                                       ActiveRequestCallbacks& callbacks) {
  ASSERT(connection_->state() == Network::Connection::State::Open);
  pending_requests_.emplace_back(callbacks);
  encoder_->encode(request, encoder_buffer_);
  connection_->write(encoder_buffer_);
  return &pending_requests_.back();
}

void ClientImpl::onData(Buffer::Instance& data) {
  try {
    decoder_->decode(data);
  } catch (ProtocolError&) {
    connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void ClientImpl::onEvent(uint32_t events) {
  if ((events & Network::ConnectionEvent::RemoteClose) ||
      (events & Network::ConnectionEvent::LocalClose)) {
    while (!pending_requests_.empty()) {
      PendingRequest& request = pending_requests_.front();
      if (!request.canceled_) {
        request.callbacks_.onFailure();
      }
      pending_requests_.pop_front();
    }
  }
}

void ClientImpl::onRespValue(RespValuePtr&& value) {
  ASSERT(!pending_requests_.empty());
  PendingRequest& request = pending_requests_.front();
  if (!request.canceled_) {
    request.callbacks_.onResponse(std::move(value));
  }
  pending_requests_.pop_front();
}

void ClientImpl::PendingRequest::cancel() {
  // If we get a cancellation, we just mark the pending request as cancelled, and then we drop
  // the response as it comes through. There is no reason to blow away the connection when the
  // remote is already responding as fast as possible.
  canceled_ = true;
}

ClientFactoryImpl ClientFactoryImpl::instance_;

ClientPtr ClientFactoryImpl::create(const std::string& cluster_name, Upstream::ClusterManager& cm) {
  return ClientImpl::create(cluster_name, cm, EncoderPtr{new EncoderImpl()}, decoder_factory_);
}

InstanceImpl::InstanceImpl(const std::string& cluster_name, Upstream::ClusterManager& cm,
                           ClientFactory& client_factory, ThreadLocal::Instance& tls)
    : cluster_name_(cluster_name), cm_(cm), client_factory_(client_factory), tls_(tls),
      tls_slot_(tls.allocateSlot()) {
  tls.set(tls_slot_, [this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectPtr {
    return std::make_shared<ThreadLocalPool>(*this, dispatcher);
  });
}

ActiveRequest* InstanceImpl::makeRequest(const std::string&, const RespValue& value,
                                         ActiveRequestCallbacks& callbacks) {
  return tls_.getTyped<ThreadLocalPool>(tls_slot_).makeRequest(value, callbacks);
}

ActiveRequest* InstanceImpl::ThreadLocalPool::makeRequest(const RespValue& request,
                                                          ActiveRequestCallbacks& callbacks) {
  if (!client_) {
    client_.reset(new ThreadLocalActiveClient(*this));
    client_->redis_client_ = parent_.client_factory_.create(parent_.cluster_name_, parent_.cm_);
    if (client_->redis_client_) {
      client_->redis_client_->addConnectionCallbacks(*client_);
    } else {
      client_.reset();
    }
  }

  if (client_) {
    // TODO: In the case of retry, this is broken. This client could be in the process of being
    //       shut down. Since the entire pool implementation is going to get reworked to support
    //       real pooling we won't worry about this now.
    return client_->redis_client_->makeRequest(request, callbacks);
  } else {
    return nullptr;
  }
}

void InstanceImpl::ThreadLocalPool::onEvent(ThreadLocalActiveClient&, uint32_t events) {
  if ((events & Network::ConnectionEvent::RemoteClose) ||
      (events & Network::ConnectionEvent::LocalClose)) {
    dispatcher_.deferredDelete(std::move(client_));
  }
}

void InstanceImpl::ThreadLocalPool::shutdown() {
  if (client_) {
    client_->redis_client_->close();
  }
}

} // ConnPool
} // Redis
