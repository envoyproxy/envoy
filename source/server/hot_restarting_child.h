#pragma once

#include "envoy/network/parent_drained_callback_registrar.h"
#include "envoy/server/instance.h"

#include "source/common/stats/stat_merger.h"
#include "source/server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The child half of hot restarting. Issues requests and commands to the parent.
 */
class HotRestartingChild : public HotRestartingBase,
                           public Network::ParentDrainedCallbackRegistrar {
public:
  // A structure to record the set of registered UDP listeners keyed on their addresses,
  // to support QUIC packet forwarding.
  class UdpForwardingContext {
  public:
    using ForwardEntry = std::pair<Network::Address::InstanceConstSharedPtr,
                                   std::shared_ptr<Network::UdpListenerConfig>>;

    // Returns the address and UdpListenerConfig associated with the given address.
    // The addresses are not necessarily identical, as e.g. the listener might be listening on
    // 0.0.0.0.
    // This is called from the thread to which the hot restart Event::Dispatcher
    // dispatches, which is expected to be the same main thread as registerListener
    // is called from.
    absl::optional<ForwardEntry>
    getListenerForDestination(const Network::Address::Instance& address);

    // Registers a UdpListenerConfig and address into the map, to be matched using
    // getListenerForDestination for UDP packet forwarding.
    // This is called from the main thread during listening socket creation.
    void registerListener(Network::Address::InstanceConstSharedPtr address,
                          std::shared_ptr<Network::UdpListenerConfig> listener_config);

  private:
    // Map keyed on address as a string, because Network::Address::Instance isn't hashable.
    absl::flat_hash_map<std::string, ForwardEntry> listener_map_;
  };

  HotRestartingChild(int base_id, int restart_epoch, const std::string& socket_path,
                     mode_t socket_mode, bool skip_hot_restart_on_no_parent,
                     bool skip_parent_stats);
  ~HotRestartingChild() override = default;

  void initialize(Event::Dispatcher& dispatcher);
  void shutdown();

  int duplicateParentListenSocket(const std::string& address, uint32_t worker_index);
  void registerUdpForwardingListener(Network::Address::InstanceConstSharedPtr address,
                                     std::shared_ptr<Network::UdpListenerConfig> listener_config);
  // From Network::ParentDrainedCallbackRegistrar.
  void registerParentDrainedCallback(const Network::Address::InstanceConstSharedPtr& addr,
                                     absl::AnyInvocable<void()> action) override;
  std::unique_ptr<envoy::HotRestartMessage> getParentStats();
  void drainParentListeners();
  absl::optional<HotRestart::AdminShutdownResponse> sendParentAdminShutdownRequest();
  void sendParentTerminateRequest();
  void mergeParentStats(Stats::Store& stats_store,
                        const envoy::HotRestartMessage::Reply::Stats& stats_proto);

protected:
  absl::Status onSocketEventUdpForwarding();
  void onForwardedUdpPacket(uint32_t worker_index, Network::UdpRecvData&& data);
  // When call to terminate parent is sent, or parent is already terminated,
  void allDrainsImplicitlyComplete();

private:
  bool abortDueToFailedParentConnection();
  friend class HotRestartUdpForwardingTestHelper;
  absl::Mutex registry_mu_;
  const int restart_epoch_;
  bool parent_terminated_;
  bool parent_drained_ ABSL_GUARDED_BY(registry_mu_);
  const bool skip_hot_restart_on_no_parent_;
  const bool skip_parent_stats_;
  sockaddr_un parent_address_;
  sockaddr_un parent_address_udp_forwarding_;
  std::unique_ptr<Stats::StatMerger> stat_merger_{};
  Stats::StatName hot_restart_generation_stat_name_;
  // There are multiple listener instances per address that must all be reactivated
  // when the parent is drained, so a multimap is used to contain them.
  std::unordered_multimap<std::string, absl::AnyInvocable<void()>>
      on_drained_actions_ ABSL_GUARDED_BY(registry_mu_);
  Event::FileEventPtr socket_event_udp_forwarding_;
  UdpForwardingContext udp_forwarding_context_;
};

} // namespace Server
} // namespace Envoy
