#pragma once

#include <atomic>

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

    // Returns the address and UdpListenerConfig associated with the given address, within the
    // address's network namespace (listeners without one are only matched by addresses without
    // one). The addresses are not necessarily identical, as e.g. the listener might be listening
    // on 0.0.0.0.
    // This is called from the thread to which the hot restart Event::Dispatcher
    // dispatches, which is expected to be the same main thread as registerListener
    // is called from.
    std::optional<ForwardEntry>
    getListenerForDestination(const Network::Address::Instance& address);

    // Registers a UdpListenerConfig and address into the map, to be matched using
    // getListenerForDestination for UDP packet forwarding.
    // This is called from the main thread during listening socket creation.
    void registerListener(Network::Address::InstanceConstSharedPtr address,
                          std::shared_ptr<Network::UdpListenerConfig> listener_config);

  private:
    // Builds the map key for an address string within a network namespace.
    static std::string key(absl::string_view network_namespace, absl::string_view address);

    // Map keyed on network namespace and address as a string, because
    // Network::Address::Instance isn't hashable.
    absl::flat_hash_map<std::string, ForwardEntry> listener_map_;
  };

  HotRestartingChild(int base_id, int restart_epoch, const std::string& socket_path,
                     mode_t socket_mode, bool skip_hot_restart_on_no_parent,
                     bool skip_parent_stats);
  ~HotRestartingChild() override = default;

  void initialize(Event::Dispatcher& dispatcher);
  void shutdown();

  int duplicateParentListenSocket(const std::string& address, uint32_t worker_index,
                                  absl::string_view network_namespace);
  void registerUdpForwardingListener(Network::Address::InstanceConstSharedPtr address,
                                     std::shared_ptr<Network::UdpListenerConfig> listener_config);
  // From Network::ParentDrainedCallbackRegistrar.
  void registerParentDrainedCallback(const Network::Address::InstanceConstSharedPtr& addr,
                                     absl::AnyInvocable<void()> action) override;
  std::unique_ptr<envoy::HotRestartMessage> getParentStats();
  void drainParentListeners();
  bool parentStopAcceptingRequested() const { return parent_stop_accepting_requested_.load(); }
  bool parentUnresponsive() const { return parent_unresponsive_; }
  std::optional<HotRestart::AdminShutdownResponse> sendParentAdminShutdownRequest();
  void sendParentTerminateRequest();
  void mergeParentStats(Stats::Store& stats_store,
                        const envoy::HotRestartMessage::Reply::Stats& stats_proto);

  // How long a request waits for the parent's reply before the parent is written off as
  // unresponsive; see waitForParentReply(). The stats and admin-shutdown exchanges get the long
  // one (a large stats export takes time, and losing the merge is costly); a listen socket
  // request gets the short one: the parent answers it from memory, and every second of waiting is
  // a second this instance's main thread (admin, xDS, timers) is stalled on a listener add.
  static constexpr std::chrono::milliseconds PARENT_REPLY_TIMEOUT{30000};
  static constexpr std::chrono::milliseconds PARENT_LISTEN_SOCKET_REPLY_TIMEOUT{5000};
  // How often, while waiting for a reply, the parent's socket is probed to detect that the parent
  // is gone (which a datagram socket does not otherwise report to the receiver).
  static constexpr std::chrono::milliseconds PARENT_PROBE_INTERVAL{1000};

protected:
  absl::Status onSocketEventUdpForwarding();
  // Delivers a packet forwarded by the parent to the listener bound to `listener_address`, or to
  // the listener for the packet's destination when the parent did not send the listener address.
  void onForwardedUdpPacket(uint32_t worker_index,
                            const Network::Address::Instance& listener_address,
                            Network::UdpRecvData&& data);
  // When call to terminate parent is sent, or parent is already terminated,
  void allDrainsImplicitlyComplete();

private:
  bool abortDueToFailedParentConnection();
  // Waits for the parent's reply to the request just sent on the main stream. The wait is not a
  // plain blocking receive: forwarded UDP packets keep being serviced meanwhile, since the parent
  // forwards them from its main thread and stalls there when our socket is full -- the same main
  // thread that has to produce the reply. It is bounded by PARENT_REPLY_TIMEOUT and by liveness
  // probes every PARENT_PROBE_INTERVAL. Returns nullptr when no reply came, after writing the
  // parent off (see onParentUnreachable) so no later request waits on it or consumes its late
  // reply.
  std::unique_ptr<envoy::HotRestartMessage> waitForParentReply(std::chrono::milliseconds timeout);
  // Stops all further request/reply exchanges with the parent: stats are no longer merged, new
  // listeners bind their own sockets, and the parent is still told to terminate at the end of
  // the drain period (that request needs no reply).
  void onParentUnreachable(absl::string_view reason);
  // The parent is definitely gone (its socket refuses our datagrams): there is nothing left to
  // drain from, so complete the drains as sendParentTerminateRequest() would.
  void onParentGone(absl::string_view reason);
  friend class HotRestartUdpForwardingTestHelper;
  absl::Mutex registry_mu_;
  const int restart_epoch_;
  bool parent_terminated_;
  // Set once the parent failed to answer a request in time; see onParentUnreachable().
  bool parent_unresponsive_{false};
  std::chrono::milliseconds parent_reply_timeout_{PARENT_REPLY_TIMEOUT};
  std::chrono::milliseconds parent_listen_socket_reply_timeout_{PARENT_LISTEN_SOCKET_REPLY_TIMEOUT};
  std::chrono::milliseconds parent_probe_interval_{PARENT_PROBE_INTERVAL};
  bool parent_drained_ ABSL_GUARDED_BY(registry_mu_);
  const bool skip_hot_restart_on_no_parent_;
  const bool skip_parent_stats_;
  sockaddr_un parent_address_;
  sockaddr_un parent_address_udp_forwarding_;
  std::unique_ptr<Stats::StatMerger> stat_merger_;
  Stats::StatName hot_restart_generation_stat_name_;
  // There are multiple listener instances per address that must all be reactivated
  // when the parent is drained, so a multimap is used to contain them.
  std::unordered_multimap<std::string, absl::AnyInvocable<void()>>
      on_drained_actions_ ABSL_GUARDED_BY(registry_mu_);
  // Whether this child has already asked the parent to stop accepting new connections, i.e. whether
  // drainParentListeners() has sent the drain-listeners request. Set on the main thread and polled
  // from worker threads, so it is atomic. Initialized true when there is no parent.
  std::atomic<bool> parent_stop_accepting_requested_;
  Event::FileEventPtr socket_event_udp_forwarding_;
  UdpForwardingContext udp_forwarding_context_;
};

} // namespace Server
} // namespace Envoy
