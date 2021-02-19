#pragma once
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

class ActiveUdpListenerBase : public ActiveListenerImplBase,
                              public Network::ConnectionHandler::ActiveUdpListener {
public:
  ActiveUdpListenerBase(uint32_t worker_index, uint32_t concurrency,
                        Network::UdpConnectionHandler& parent, Network::Socket& listen_socket,
                        Network::UdpListenerPtr&& listener, Network::ListenerConfig* config);
  ~ActiveUdpListenerBase() override;

  // Network::UdpListenerCallbacks
  void onData(Network::UdpRecvData&& data) final;
  uint32_t workerIndex() const final { return worker_index_; }
  void post(Network::UdpRecvData&& data) final;

  // ActiveListenerImplBase
  Network::Listener* listener() override { return udp_listener_.get(); }

protected:
  uint32_t destination(const Network::UdpRecvData& /*data*/) const override {
    // By default, route to the current worker.
    return worker_index_;
  }

  const uint32_t worker_index_;
  const uint32_t concurrency_;
  Network::UdpConnectionHandler& parent_;
  Network::Socket& listen_socket_;
  Network::UdpListenerPtr udp_listener_;
};

/**
 * Wrapper for an active udp listener owned by this handler.
 */
class ActiveRawUdpListener : public ActiveUdpListenerBase,
                             public Network::UdpListenerFilterManager,
                             public Network::UdpReadFilterCallbacks {
public:
  ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                       Network::UdpConnectionHandler& parent, Event::Dispatcher& dispatcher,
                       Network::ListenerConfig& config);
  ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                       Network::UdpConnectionHandler& parent,
                       Network::SocketSharedPtr listen_socket_ptr, Event::Dispatcher& dispatcher,
                       Network::ListenerConfig& config);
  ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                       Network::UdpConnectionHandler& parent, Network::Socket& listen_socket,
                       Network::SocketSharedPtr listen_socket_ptr, Event::Dispatcher& dispatcher,
                       Network::ListenerConfig& config);
  ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                       Network::UdpConnectionHandler& parent, Network::Socket& listen_socket,
                       Network::UdpListenerPtr&& listener, Network::ListenerConfig& config);

  // Network::UdpListenerCallbacks
  void onReadReady() override;
  void onWriteReady(const Network::Socket& socket) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;
  Network::UdpPacketWriter& udpPacketWriter() override { return *udp_packet_writer_; }

  // Network::UdpWorker
  void onDataWorker(Network::UdpRecvData&& data) override;

  // ActiveListenerImplBase
  void pauseListening() override { udp_listener_->disable(); }
  void resumeListening() override { udp_listener_->enable(); }
  void shutdownListener() override {
    // The read filter should be deleted before the UDP listener is deleted.
    // The read filter refers to the UDP listener to send packets to downstream.
    // If the UDP listener is deleted before the read filter, the read filter may try to use it
    // after deletion.
    read_filter_.reset();
    udp_listener_.reset();
  }

  // Network::UdpListenerFilterManager
  void addReadFilter(Network::UdpListenerReadFilterPtr&& filter) override;

  // Network::UdpReadFilterCallbacks
  Network::UdpListener& udpListener() override;

private:
  Network::UdpListenerReadFilterPtr read_filter_;
  Network::UdpPacketWriterPtr udp_packet_writer_;
};

} // namespace Server
} // namespace Envoy
