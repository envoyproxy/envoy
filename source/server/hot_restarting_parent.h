#pragma once

#include "envoy/network/connection_handler.h"

#include "source/common/common/hash.h"
#include "source/server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

class HotRestartMessageSender {
public:
  virtual void sendHotRestartMessage(envoy::HotRestartMessage&& msg) PURE;
  virtual ~HotRestartMessageSender() = default;
};

/**
 * The parent half of hot restarting. Listens for requests and commands from the child.
 * This outer class only handles evented socket I/O. The actual hot restart logic lives in
 * HotRestartingParent::Internal.
 */
class HotRestartingParent : public HotRestartingBase, public HotRestartMessageSender {
public:
  HotRestartingParent(int base_id, int restart_epoch, const std::string& socket_path,
                      mode_t socket_mode);
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server);
  void shutdown();
  // HotRestartMessageSender: forwards a UDP packet message to the child on the UDP forwarding
  // stream, from the dispatcher's thread, without ever blocking on the child. Datagrams the
  // child's socket cannot take right now are queued (bounded) and retried shortly; the packet is
  // dropped when the queue is full or the child is gone. The parent's main thread must not wait
  // on the child here: the child's main thread waits on the parent for its replies, and a mutual
  // wait wedges both processes for good.
  void sendHotRestartMessage(envoy::HotRestartMessage&& msg) override;

  // Interval at which a backlog of forwarded UDP datagrams is retried while the child's socket
  // is full.
  static constexpr std::chrono::milliseconds UDP_FORWARDING_RETRY_INTERVAL{2};

  // The hot restarting parent's hot restart logic. Each function is meant to be called to fulfill a
  // request from the child for that action.
  class Internal : public Network::NonDispatchedUdpPacketHandler {
  public:
    explicit Internal(Server::Instance* server, HotRestartMessageSender& udp_sender);
    // Return value is the response to return to the child.
    envoy::HotRestartMessage shutdownAdmin();
    // Return value is the response to return to the child.
    envoy::HotRestartMessage
    getListenSocketsForChild(const envoy::HotRestartMessage::Request& request);
    // 'stats' is a field in the reply protobuf to be sent to the child, which we should populate.
    void exportStatsToChild(envoy::HotRestartMessage::Reply::Stats* stats);
    void recordDynamics(envoy::HotRestartMessage::Reply::Stats* stats, const std::string& name,
                        Stats::StatName stat_name);
    void drainListeners();

    // Network::NonDispatchedUdpPacketHandler
    void handle(uint32_t worker_index, const Network::UdpRecvData& packet) override;

  private:
    Server::Instance* const server_{};
    HotRestartMessageSender& udp_sender_;
  };

private:
  friend class HotRestartParentUdpForwardingTestHelper;
  void onSocketEvent();
  void flushUdpForwarding();

  const int restart_epoch_;
  sockaddr_un child_address_;
  sockaddr_un child_address_udp_forwarding_;
  Event::FileEventPtr socket_event_;
  Event::TimerPtr udp_forwarding_retry_timer_;
  OptRef<Event::Dispatcher> dispatcher_;
  std::unique_ptr<Internal> internal_;
  // Forwarded UDP datagrams handed to the child's socket, flush attempts that found it full, and
  // datagrams dropped (queue full, or the child gone). See sendHotRestartMessage().
  Stats::Counter* udp_forwarding_datagrams_{};
  Stats::Counter* udp_forwarding_retries_{};
  Stats::Counter* udp_forwarding_dropped_{};
};

} // namespace Server
} // namespace Envoy
