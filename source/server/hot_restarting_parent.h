#pragma once

#include "common/common/hash.h"

#include "server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The parent half of hot restarting. Listens for requests and commands from the child.
 * This outer class only handles evented socket I/O. The actual hot restart logic lives in
 * HotRestartingParent::Internal.
 */
class HotRestartingParent : HotRestartingBase, Logger::Loggable<Logger::Id::main> {
public:
  HotRestartingParent(int base_id, int restart_epoch);
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server);
  void shutdown();

  // The hot restarting parent's hot restart logic. Each function is meant to be called to fulfill a
  // request from the child for that action.
  class Internal {
  public:
    explicit Internal(Server::Instance* server);
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

  private:
    Server::Instance* const server_{};
  };

private:
  void onSocketEvent();

  const int restart_epoch_;
  sockaddr_un child_address_;
  Event::FileEventPtr socket_event_;
  std::unique_ptr<Internal> internal_;
};

} // namespace Server
} // namespace Envoy
