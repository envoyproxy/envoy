#include "source/server/hot_restarting_child.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

HotRestartingChild::HotRestartingChild(int base_id, int restart_epoch,
                                       const std::string& socket_path, mode_t socket_mode)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  initDomainSocketAddress(&parent_address_);
  if (restart_epoch_ != 0) {
    parent_address_ =
        createDomainSocketAddress(restart_epoch_ + -1, "parent", socket_path, socket_mode);
  }
  bindDomainSocket(restart_epoch_, "child", socket_path, socket_mode);
}

int HotRestartingChild::duplicateParentListenSocket(const std::string& address,
                                                    uint32_t worker_index) {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return -1;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_address(address);
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_worker_index(worker_index);
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(Blocking::Yes);
  if (!replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kPassListenSocket)) {
    return -1;
  }
  return wrapped_reply->reply().pass_listen_socket().fd();
}

std::unique_ptr<HotRestartMessage> HotRestartingChild::getParentStats() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return nullptr;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_stats();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(Blocking::Yes);
  RELEASE_ASSERT(replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kStats),
                 "Hot restart parent did not respond as expected to get stats request.");
  return wrapped_reply;
}

void HotRestartingChild::drainParentListeners() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  // No reply expected.
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_drain_listeners();
  sendHotRestartMessage(parent_address_, wrapped_request);
}

absl::optional<HotRestart::AdminShutdownResponse>
HotRestartingChild::sendParentAdminShutdownRequest() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return absl::nullopt;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_shutdown_admin();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(Blocking::Yes);
  RELEASE_ASSERT(replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kShutdownAdmin),
                 "Hot restart parent did not respond as expected to ShutdownParentAdmin.");
  return HotRestart::AdminShutdownResponse{
      static_cast<time_t>(
          wrapped_reply->reply().shutdown_admin().original_start_time_unix_seconds()),
      wrapped_reply->reply().shutdown_admin().enable_reuse_port_default()};
}

void HotRestartingChild::sendParentTerminateRequest() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_terminate();
  sendHotRestartMessage(parent_address_, wrapped_request);
  parent_terminated_ = true;

  // Note that the 'generation' counter needs to retain the contribution from
  // the parent.
  stat_merger_->retainParentGaugeValue(hot_restart_generation_stat_name_);

  // Now it is safe to forget our stat transferral state.
  //
  // This destruction is actually important far beyond memory efficiency. The
  // scope-based temporary counter logic relies on the StatMerger getting
  // destroyed once hot restart's stat merging is all done. (See stat_merger.h
  // for details).
  stat_merger_.reset();
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

} // namespace Server
} // namespace Envoy
