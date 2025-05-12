#pragma once

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
  void sendHotRestartMessage(envoy::HotRestartMessage&& msg) override;

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
  void onSocketEvent();

  const int restart_epoch_;
  sockaddr_un child_address_;
  sockaddr_un child_address_udp_forwarding_;
  Event::FileEventPtr socket_event_;
  OptRef<Event::Dispatcher> dispatcher_;
  std::unique_ptr<Internal> internal_;
};

} // namespace Server
} // namespace Envoy
