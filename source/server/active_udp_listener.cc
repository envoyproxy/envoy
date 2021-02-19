#include "server/active_udp_listener.h"

#include "envoy/network/exception.h"

#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {
ActiveUdpListenerBase::ActiveUdpListenerBase(uint32_t worker_index, uint32_t concurrency,
                                             Network::UdpConnectionHandler& parent,
                                             Network::Socket& listen_socket,
                                             Network::UdpListenerPtr&& listener,
                                             Network::ListenerConfig* config)
    : ActiveListenerImplBase(parent, config), worker_index_(worker_index),
      concurrency_(concurrency), parent_(parent), listen_socket_(listen_socket),
      udp_listener_(std::move(listener)) {
  ASSERT(worker_index_ < concurrency_);
  config_->udpListenerWorkerRouter()->get().registerWorkerForListener(*this);
}

ActiveUdpListenerBase::~ActiveUdpListenerBase() {
  config_->udpListenerWorkerRouter()->get().unregisterWorkerForListener(*this);
}

void ActiveUdpListenerBase::post(Network::UdpRecvData&& data) {
  ASSERT(!udp_listener_->dispatcher().isThreadSafe(),
         "Shouldn't be posting if thread safe; use onWorkerData() instead.");

  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  auto data_to_post = std::make_shared<Network::UdpRecvData>();
  *data_to_post = std::move(data);

  udp_listener_->dispatcher().post(
      [data_to_post, tag = config_->listenerTag(), &parent = parent_]() {
        Network::UdpListenerCallbacksOptRef listener = parent.getUdpListenerCallbacks(tag);
        if (listener.has_value()) {
          listener->get().onDataWorker(std::move(*data_to_post));
        }
      });
}

void ActiveUdpListenerBase::onData(Network::UdpRecvData&& data) {
  uint32_t dest = worker_index_;

  // For concurrency == 1, the packet will always go to the current worker.
  if (concurrency_ > 1) {
    dest = destination(data);
    ASSERT(dest < concurrency_);
  }

  if (dest == worker_index_) {
    onDataWorker(std::move(data));
  } else {
    config_->udpListenerWorkerRouter()->get().deliver(dest, std::move(data));
  }
}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent,
                           config.listenSocketFactory().getListenSocket(), dispatcher, config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, *listen_socket_ptr, listen_socket_ptr,
                           dispatcher, config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, listen_socket,
                           dispatcher.createUdpListener(listen_socket_ptr, *this), config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::UdpListenerPtr&& listener,
                                           Network::ListenerConfig& config)
    : ActiveUdpListenerBase(worker_index, concurrency, parent, listen_socket, std::move(listener),
                            &config),
      read_filter_(nullptr) {
  // Create the filter chain on creating a new udp listener
  config_->filterChainFactory().createUdpListenerFilterChain(*this, *this);

  // If filter is nullptr, fail the creation of the listener
  if (read_filter_ == nullptr) {
    throw Network::CreateListenerException(
        fmt::format("Cannot create listener as no read filter registered for the udp listener: {} ",
                    config_->name()));
  }

  // Create udp_packet_writer
  udp_packet_writer_ = config.udpPacketWriterFactory()->get().createUdpPacketWriter(
      listen_socket_.ioHandle(), config.listenerScope());
}

void ActiveRawUdpListener::onDataWorker(Network::UdpRecvData&& data) { read_filter_->onData(data); }

void ActiveRawUdpListener::onReadReady() {}

void ActiveRawUdpListener::onWriteReady(const Network::Socket&) {
  // TODO(sumukhs): This is not used now. When write filters are implemented, this is a
  // trigger to invoke the on write ready API on the filters which is when they can write
  // data.

  // Clear write_blocked_ status for udpPacketWriter.
  udp_packet_writer_->setWritable();
}

void ActiveRawUdpListener::onReceiveError(Api::IoError::IoErrorCode error_code) {
  read_filter_->onReceiveError(error_code);
}

void ActiveRawUdpListener::addReadFilter(Network::UdpListenerReadFilterPtr&& filter) {
  ASSERT(read_filter_ == nullptr, "Cannot add a 2nd UDP read filter");
  read_filter_ = std::move(filter);
}

Network::UdpListener& ActiveRawUdpListener::udpListener() { return *udp_listener_; }
} // namespace Server
} // namespace Envoy