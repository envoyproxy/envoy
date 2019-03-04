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
  if (!replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kStats)) {
    ENVOY_LOG(error, "Did not get a StatsReply for our StatsRequest. Will not merge stats from hot restart parent.");
    return nullptr;
  }
  return wrapped_reply;
}

void HotRestartingChild::drainParentListeners() {
  if (restart_epoch_ > 0) {
    // No reply expected.
    HotRestartMessage wrapped_request;
    wrapped_request.mutable_request()->mutable_drain_listeners();
    sendHotRestartMessage(parent_address_, wrapped_request);
  }
}

void HotRestartingChild::shutdownParentAdmin(HotRestart::ShutdownParentAdminInfo& info) {
  if (restart_epoch_ == 0) {
    return;
  }

  HotRestartMessage wrapped_request;
  wrapped_request.mutable_request()->mutable_shutdown_admin();
  sendHotRestartMessage(parent_address_, wrapped_request);

  std::unique_ptr<HotRestartMessage> wrapped_reply = receiveHotRestartMessage(Blocking::Yes);
  RELEASE_ASSERT(replyIsExpectedType(wrapped_reply.get(), HotRestartMessage::Reply::kShutdownAdmin),
                 "Parent did not respond as expected to ShutdownParentAdmin.");
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
  // TODO TODO is it safe to assume that at this point we will receive no more stat updates from the parent? if so, here would be a good place to empty out the "cur parent stats values" map
}

} // namespace Server
} // namespace Envoy
