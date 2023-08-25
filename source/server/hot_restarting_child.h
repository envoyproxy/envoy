#pragma once

#include "envoy/server/instance.h"

#include "source/common/stats/stat_merger.h"
#include "source/server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The child half of hot restarting. Issues requests and commands to the parent.
 */
class HotRestartingChild : public HotRestartingBase {
public:
  HotRestartingChild(int base_id, int restart_epoch, const std::string& socket_path,
                     mode_t socket_mode);

  void initialize(Event::Dispatcher& dispatcher);
  void shutdown();
  int duplicateParentListenSocket(const std::string& address, uint32_t worker_index);
  std::unique_ptr<envoy::HotRestartMessage> getParentStats();
  void drainParentListeners();
  absl::optional<HotRestart::AdminShutdownResponse> sendParentAdminShutdownRequest();
  void sendParentTerminateRequest();
  void mergeParentStats(Stats::Store& stats_store,
                        const envoy::HotRestartMessage::Reply::Stats& stats_proto);

protected:
  class UdpForwardingContext;
  void onSocketEventUdpForwarding();
  void onForwardedUdpPacket(uint32_t worker_index, Network::UdpRecvData&& data);

private:
  const int restart_epoch_;
  bool parent_terminated_{};
  sockaddr_un parent_address_;
  sockaddr_un parent_address_udp_forwarding_;
  std::unique_ptr<Stats::StatMerger> stat_merger_{};
  Stats::StatName hot_restart_generation_stat_name_;
  Event::FileEventPtr socket_event_udp_forwarding_;
  std::unique_ptr<UdpForwardingContext> udp_forwarding_context_;
};

} // namespace Server
} // namespace Envoy
