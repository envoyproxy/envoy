#include "server/hot_restarting_child.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::admin::v2alpha::HotRestartMessage;

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
  wrapped_request.mutable_pass_listen_socket_request()->set_address(address);
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(/*blocking=*/true);
  if (!wrapped_reply || wrapped_reply->reply_case() != HotRestartMessage::kPassListenSocketReply) {
    return -1;
  }
  return wrapped_reply->pass_listen_socket_reply().fd();
}

void HotRestartingChild::getParentStats(HotRestart::GetParentStatsInfo& info) {
  memset(&info, 0, sizeof(info));
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_stats_request();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(/*blocking=*/true);
  RELEASE_ASSERT(wrapped_reply && wrapped_reply->reply_case() == HotRestartMessage::kStatsReply,
                 "Did not get a StatsReply for our StatsRequest.");
  // TODO(fredlas) this is where the stat transferring, to be added later in this PR, will go.
  info.memory_allocated_ = wrapped_reply->stats_reply().memory_allocated();
  info.num_connections_ = wrapped_reply->stats_reply().num_connections();
}

void HotRestartingChild::drainParentListeners() {
  if (restart_epoch_ > 0) {
    // No reply expected.
    HotRestartMessage wrapped_request;
    wrapped_request.mutable_drain_listeners_request();
    sendHotRestartMessage(parent_address_, wrapped_request);
  }
}

void HotRestartingChild::shutdownParentAdmin(HotRestart::ShutdownParentAdminInfo& info) {
  if (restart_epoch_ == 0) {
    return;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_shutdown_admin_request();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(/*blocking=*/true);
  RELEASE_ASSERT(wrapped_reply &&
                     wrapped_reply->reply_case() == HotRestartMessage::kShutdownAdminReply,
                 "Parent did not respond as expected to ShutdownParentAdmin.");
  info.original_start_time_ = wrapped_reply->shutdown_admin_reply().original_start_time();
}

void HotRestartingChild::terminateParent() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }
  HotRestartMessage wrapped_request;
  wrapped_request.mutable_terminate_request();
  sendHotRestartMessage(parent_address_, wrapped_request);
  parent_terminated_ = true;
}

} // namespace Server
} // namespace Envoy
