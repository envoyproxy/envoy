#include "source/server/hot_restarting_child.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

void HotRestartingChild::UdpForwardingContext::registerListener(
    Network::Address::InstanceConstSharedPtr address,
    std::shared_ptr<Network::UdpListenerConfig> listener_config) {
  const bool inserted =
      listener_map_.try_emplace(address->asString(), ForwardEntry{address, listener_config}).second;
  ASSERT(inserted, "Two udp listeners on the same address shouldn't be possible");
}

absl::optional<HotRestartingChild::UdpForwardingContext::ForwardEntry>
HotRestartingChild::UdpForwardingContext::getListenerForDestination(
    const Network::Address::Instance& address) {
  auto it = listener_map_.find(address.asString());
  if (it == listener_map_.end()) {
    // If no listener on the specific address was found, check for a default route.
    // If the address is IPv6, check default route IPv6 only, otherwise check default
    // route IPv4 then default route IPv6, as either can potentially receive an IPv4
    // packet.
    uint32_t port = address.ip()->port();
    if (address.ip()->version() == Network::Address::IpVersion::v6) {
      it = listener_map_.find(absl::StrCat("[::]:", port));
    } else {
      it = listener_map_.find(absl::StrCat("0.0.0.0:", port));
      if (it == listener_map_.end()) {
        it = listener_map_.find(absl::StrCat("[::]:", port));
        if (it != listener_map_.end() && it->second.first->ip()->ipv6()->v6only()) {
          // If there is a default IPv6 route but it's set v6only, don't use it.
          it = listener_map_.end();
        }
      }
    }
  }
  if (it == listener_map_.end()) {
    return absl::nullopt;
  }
  return it->second;
}

// If restart_epoch is 0 there is no parent, so it's effectively already
// drained and terminated.
HotRestartingChild::HotRestartingChild(int base_id, int restart_epoch,
                                       const std::string& socket_path, mode_t socket_mode,
                                       bool skip_hot_restart_on_no_parent, bool skip_parent_stats)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch),
      parent_terminated_(restart_epoch == 0), parent_drained_(restart_epoch == 0),
      skip_hot_restart_on_no_parent_(skip_hot_restart_on_no_parent),
      skip_parent_stats_(skip_parent_stats) {
  main_rpc_stream_.initDomainSocketAddress(&parent_address_);
  std::string socket_path_udp = socket_path + "_udp";
  udp_forwarding_rpc_stream_.initDomainSocketAddress(&parent_address_udp_forwarding_);
  if (restart_epoch_ != 0) {
    parent_address_ = main_rpc_stream_.createDomainSocketAddress(restart_epoch_ + -1, "parent",
                                                                 socket_path, socket_mode);
    parent_address_udp_forwarding_ = udp_forwarding_rpc_stream_.createDomainSocketAddress(
        restart_epoch_ + -1, "parent", socket_path_udp, socket_mode);
  }
  main_rpc_stream_.bindDomainSocket(restart_epoch_, "child", socket_path, socket_mode);
  udp_forwarding_rpc_stream_.bindDomainSocket(restart_epoch_, "child", socket_path_udp,
                                              socket_mode);
}

bool HotRestartingChild::abortDueToFailedParentConnection() {
  if (!skip_hot_restart_on_no_parent_) {
    return false;
  }
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_test_connection();
  if (!main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request, true)) {
    return true;
  }
  return false;
}

void HotRestartingChild::initialize(Event::Dispatcher& dispatcher) {
  if (abortDueToFailedParentConnection()) {
    ENVOY_LOG(warn, "hot restart sendmsg() connection refused, falling back to regular restart");
    absl::MutexLock lock(&registry_mu_);
    parent_terminated_ = parent_drained_ = true;
    return;
  }
  socket_event_udp_forwarding_ = dispatcher.createFileEvent(
      udp_forwarding_rpc_stream_.domain_socket_,
      [this](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        return onSocketEventUdpForwarding();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

void HotRestartingChild::shutdown() { socket_event_udp_forwarding_.reset(); }

void HotRestartingChild::onForwardedUdpPacket(uint32_t worker_index, Network::UdpRecvData&& data) {
  auto addr_and_listener =
      udp_forwarding_context_.getListenerForDestination(*data.addresses_.local_);
  if (addr_and_listener.has_value()) {
    auto [addr, listener_config] = *addr_and_listener;
    // We send to the worker index from the parent instance.
    // In the case that the number of workers changes between instances,
    // or the quic connection id generator changes how it selects worker
    // ids, the hot restart packet transfer will fail.
    //
    // One option would be to dispatch an onData call to have the receiving
    // worker forward the packet if the calculated destination differs from
    // the parent instance worker index; however, this would require
    // temporarily disabling kernel_worker_routing_ in each instance of
    // ActiveQuicListener, and a much more convoluted pipeline to collect
    // the set of destinations (listenerWorkerRouter doesn't currently
    // expose the actual listeners.)
    //
    // Since the vast majority of hot restarts will change neither of these
    // things, this implementation is "pretty good", and much better than no
    // hot restart capability at all.
    listener_config->listenerWorkerRouter(*addr).deliver(worker_index, std::move(data));
  }
}

int HotRestartingChild::duplicateParentListenSocket(const std::string& address,
                                                    uint32_t worker_index) {
  if (parent_terminated_) {
    return -1;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_address(address);
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_worker_index(worker_index);
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  if (!main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(),
                                            HotRestartMessage::Reply::kPassListenSocket)) {
    return -1;
  }
  return wrapped_reply->reply().pass_listen_socket().fd();
}

std::unique_ptr<HotRestartMessage> HotRestartingChild::getParentStats() {
  if (parent_terminated_ || skip_parent_stats_) {
    return nullptr;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_stats();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  RELEASE_ASSERT(
      main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kStats),
      "Hot restart parent did not respond as expected to get stats request.");
  return wrapped_reply;
}

void HotRestartingChild::drainParentListeners() {
  if (parent_terminated_) {
    return;
  }
  // No reply expected.
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_drain_listeners();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);
}

void HotRestartingChild::registerUdpForwardingListener(
    Network::Address::InstanceConstSharedPtr address,
    std::shared_ptr<Network::UdpListenerConfig> listener_config) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  udp_forwarding_context_.registerListener(address, listener_config);
}

void HotRestartingChild::registerParentDrainedCallback(
    const Network::Address::InstanceConstSharedPtr& address, absl::AnyInvocable<void()> callback) {
  absl::MutexLock lock(&registry_mu_);
  if (parent_drained_) {
    callback();
  } else {
    on_drained_actions_.emplace(address->asString(), std::move(callback));
  }
}

void HotRestartingChild::allDrainsImplicitlyComplete() {
  absl::MutexLock lock(&registry_mu_);
  for (auto& drain_action : on_drained_actions_) {
    // Call the callback.
    std::move(drain_action.second)();
  }
  on_drained_actions_.clear();
  parent_drained_ = true;
}

absl::optional<HotRestart::AdminShutdownResponse>
HotRestartingChild::sendParentAdminShutdownRequest() {
  if (parent_terminated_) {
    return absl::nullopt;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_shutdown_admin();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply =
      main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  RELEASE_ASSERT(main_rpc_stream_.replyIsExpectedType(wrapped_reply.get(),
                                                      HotRestartMessage::Reply::kShutdownAdmin),
                 "Hot restart parent did not respond as expected to ShutdownParentAdmin.");
  return HotRestart::AdminShutdownResponse{
      static_cast<time_t>(
          wrapped_reply->reply().shutdown_admin().original_start_time_unix_seconds()),
      wrapped_reply->reply().shutdown_admin().enable_reuse_port_default()};
}

void HotRestartingChild::sendParentTerminateRequest() {
  if (parent_terminated_) {
    return;
  }
  allDrainsImplicitlyComplete();

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_terminate();
  main_rpc_stream_.sendHotRestartMessage(parent_address_, wrapped_request);
  parent_terminated_ = true;

  // Note that the 'generation' counter needs to retain the contribution from
  // the parent.
  if (stat_merger_ != nullptr) {
    stat_merger_->retainParentGaugeValue(hot_restart_generation_stat_name_);

    // Now it is safe to forget our stat transferral state.
    //
    // This destruction is actually important far beyond memory efficiency. The
    // scope-based temporary counter logic relies on the StatMerger getting
    // destroyed once hot restart's stat merging is all done. (See stat_merger.h
    // for details).
    stat_merger_.reset();
  }
}

void HotRestartingChild::mergeParentStats(Stats::Store& stats_store,
                                          const HotRestartMessage::Reply::Stats& stats_proto) {
  if (!stat_merger_) {
    stat_merger_ = std::make_unique<Stats::StatMerger>(stats_store);
    hot_restart_generation_stat_name_ = hotRestartGeneration(*stats_store.rootScope()).statName();
  }

  // Convert the protobuf for serialized dynamic spans into the structure
  // required by StatMerger.
  Stats::StatMerger::DynamicsMap dynamics;
  for (const auto& iter : stats_proto.dynamics()) {
    Stats::DynamicSpans& spans = dynamics[iter.first];
    for (int i = 0; i < iter.second.spans_size(); ++i) {
      const HotRestartMessage::Reply::Span& span_proto = iter.second.spans(i);
      spans.push_back(Stats::DynamicSpan(span_proto.first(), span_proto.last()));
    }
  }
  stat_merger_->mergeStats(stats_proto.counter_deltas(), stats_proto.gauges(), dynamics);
}

absl::Status HotRestartingChild::onSocketEventUdpForwarding() {
  std::unique_ptr<HotRestartMessage> wrapped_request;
  while ((wrapped_request =
              udp_forwarding_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::No))) {
    if (wrapped_request->requestreply_case() == HotRestartMessage::kReply) {
      ENVOY_LOG_PERIODIC(
          error, std::chrono::seconds(5),
          "HotRestartMessage reply received on UdpForwarding (we want only requests); ignoring.");
      continue;
    }
    switch (wrapped_request->request().request_case()) {
    case HotRestartMessage::Request::kForwardedUdpPacket: {
      const auto& req = wrapped_request->request().forwarded_udp_packet();
      Network::UdpRecvData packet;
      auto local_or_error = Network::Utility::resolveUrl(req.local_addr());
      RETURN_IF_NOT_OK(local_or_error.status());
      packet.addresses_.local_ = *local_or_error;
      auto peer_or_error = Network::Utility::resolveUrl(req.peer_addr());
      RETURN_IF_NOT_OK(peer_or_error.status());
      packet.addresses_.peer_ = *peer_or_error;
      if (!packet.addresses_.local_ || !packet.addresses_.peer_) {
        break;
      }
      packet.receive_time_ =
          MonotonicTime(std::chrono::microseconds{req.receive_time_epoch_microseconds()});
      packet.buffer_ = std::make_unique<Buffer::OwnedImpl>(req.payload());
      onForwardedUdpPacket(req.worker_index(), std::move(packet));
      break;
    }
    default: {
      ENVOY_LOG(
          error,
          "child sent a request other than ForwardedUdpPacket on udp forwarding socket; ignoring.");
      break;
    }
    }
  }
  return absl::OkStatus();
}

} // namespace Server
} // namespace Envoy
