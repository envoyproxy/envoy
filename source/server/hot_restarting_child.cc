#include "server/hot_restarting_child.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::api::v2::core::HotRestartMessage;

HotRestartingChild::HotRestartingChild(int base_id, int restart_epoch)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  initDomainSocketAddress(&parent_address_);
  if (restart_epoch_ != 0) {
    parent_address_ = createDomainSocketAddress(restart_epoch_ + -1, "parent");
  }
  bindDomainSocket(restart_epoch_, "child");
}

int HotRestartingChild::duplicateParentListenSocket(const std::string& address) {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return -1;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_pass_listen_socket()->set_address(address);
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
  RELEASE_ASSERT(replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kShutdownAdmin),
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

void HotRestartingChild::shutdownParentAdmin(HotRestart::ShutdownParentAdminInfo& info) {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_shutdown_admin();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(Blocking::Yes);
  RELEASE_ASSERT(replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kShutdownAdmin),
                 "Hot restart parent did not respond as expected to ShutdownParentAdmin.");
  info.original_start_time_ = wrapped_reply->reply().shutdown_admin().original_start_time();
}

void HotRestartingChild::terminateParent() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_terminate();
  sendHotRestartMessage(parent_address_, wrapped_request);
  parent_terminated_ = true;
  // Once setting parent_terminated_ == true, we can send no more hot restart RPCs, and therefore
  // receive no more responses, including stats. So, now safe to forget our stat transferral state.
  parent_counter_values_.clear();
  parent_gauge_values_.clear();
}

void HotRestartingChild::mergeParentStats(Stats::Store& stats_store,
                                          const HotRestartMessage::Reply::Stats& stats_proto) {
  // Map from stat name *substrings* to special logic to use for combining stat values.
  const std::unordered_map<std::string, CombineLogic> combine_logic_exceptions{
      {".version", CombineLogic::NoImport},
      {"connected_state", CombineLogic::BooleanOr},
  };
  for (const auto& counter_proto : stats_proto.counters()) {
    uint64_t new_parent_value = counter_proto.value();
    auto found_value = parent_counter_values_.find(counter_proto.name());
    uint64_t old_parent_value =
        found_value == parent_counter_values_.end() ? 0 : found_value->second;
    parent_counter_values_[counter_proto.name()] = new_parent_value;
    stats_store.counter(counter_proto.name()).add(new_parent_value - old_parent_value);
  }

  for (const auto& gauge_proto : stats_proto.gauges()) {
    uint64_t new_parent_value = gauge_proto.value();
    auto found_value = parent_gauge_values_.find(gauge_proto.name());
    uint64_t old_parent_value = found_value == parent_gauge_values_.end() ? 0 : found_value->second;
    parent_gauge_values_[gauge_proto.name()] = new_parent_value;
    CombineLogic combine_logic = CombineLogic::Accumulate;
    for (auto exception : combine_logic_exceptions) {
      if (gauge_proto.name().find(exception.first) != std::string::npos) {
        combine_logic = exception.second;
        break;
      }
    }
    if (combine_logic == CombineLogic::NoImport) {
      continue;
    }
    auto& gauge_ref = stats_store.gauge(gauge_proto.name());
    // If undefined, take parent's value unless explicitly told NoImport.
    if (!gauge_ref.used()) {
      gauge_ref.set(new_parent_value);
      continue;
    }
    switch (combine_logic) {
    case CombineLogic::OnlyImportWhenUnused:
      // Already set above; nothing left to do.
      break;
    case CombineLogic::NoImport:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    case CombineLogic::Accumulate:
      if (new_parent_value > old_parent_value) {
        gauge_ref.add(new_parent_value - old_parent_value);
      } else {
        gauge_ref.sub(old_parent_value - new_parent_value);
      }
      break;
    case CombineLogic::BooleanAnd:
      gauge_ref.set(gauge_ref.value() != 0 && new_parent_value != 0 ? 1 : 0);
      break;
    case CombineLogic::BooleanOr:
      gauge_ref.set(gauge_ref.value() != 0 || new_parent_value != 0 ? 1 : 0);
      break;
    }
  }
}

} // namespace Server
} // namespace Envoy
