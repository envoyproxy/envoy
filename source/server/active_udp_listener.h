#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "envoy/network/connection_handler.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "source/common/network/utility.h"
#include "source/server/active_listener_base.h"

namespace Envoy {
namespace Server {

#define ALL_UDP_LISTENER_STATS(COUNTER) COUNTER(downstream_rx_datagram_dropped)

/**
 * Wrapper struct for UDP listener stats. @see stats_macros.h
 */
struct UdpListenerStats {
  ALL_UDP_LISTENER_STATS(GENERATE_COUNTER_STRUCT)
};

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
  void onDatagramsDropped(uint32_t dropped) final {
    udp_stats_.downstream_rx_datagram_dropped_.add(dropped);
  }

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
  UdpListenerStats udp_stats_;
  Network::UdpListenerWorkerRouter& udp_listener_worker_router_;
};

/**
 * Wrapper for an active udp listener owned by this handler.
 */
class ActiveRawUdpListener : public ActiveUdpListenerBase,
                             public Network::UdpListenerFilterManager,
                             public Network::UdpReadFilterCallbacks,
                             Logger::Loggable<Logger::Id::conn_handler> {
public:
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
  size_t numPacketsExpectedPerEventLoop() const final {
    // TODO(mattklein123) change this to a reasonable number if needed.
    return Network::MAX_NUM_PACKETS_PER_EVENT_LOOP;
  }

  // Network::UdpWorker
  void onDataWorker(Network::UdpRecvData&& data) override;

  // ActiveListenerImplBase
  void pauseListening() override { udp_listener_->disable(); }
  void resumeListening() override { udp_listener_->enable(); }
  void shutdownListener(const Network::ExtraShutdownListenerOptions&) override {
    // The read filter should be deleted before the UDP listener is deleted.
    // The read filter refers to the UDP listener to send packets to downstream.
    // If the UDP listener is deleted before the read filter, the read filter may try to use it
    // after deletion.
    read_filters_.clear();
    udp_listener_.reset();
  }
  // These two are unreachable because a config will be rejected if it configures both this listener
  // and any L4 filter chain.
  void updateListenerConfig(Network::ListenerConfig&) override {
    IS_ENVOY_BUG("unexpected call to updateListenerConfig");
  }
  void onFilterChainDraining(const std::list<const Network::FilterChain*>&) override {
    IS_ENVOY_BUG("unexpected call to onFilterChainDraining");
  }

  // Network::UdpListenerFilterManager
  void addReadFilter(Network::UdpListenerReadFilterPtr&& filter) override;

  // Network::UdpReadFilterCallbacks
  Network::UdpListener& udpListener() override;

private:
  std::list<Network::UdpListenerReadFilterPtr> read_filters_;
  Network::UdpPacketWriterPtr udp_packet_writer_;
};

} // namespace Server
} // namespace Envoy
