#include "server/hot_restarting_child.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Server {

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

  RpcGetListenSocketRequest rpc;
  ASSERT(address.length() < sizeof(rpc.address_));
  StringUtil::strlcpy(rpc.address_, address.c_str(), sizeof(rpc.address_));
  sendMessage(parent_address_, rpc);
  RpcGetListenSocketReply* reply =
      receiveTypedRpc<RpcGetListenSocketReply, RpcMessageType::GetListenSocketReply>();
  return reply->fd_;
}

void HotRestartingChild::getParentStats(HotRestart::GetParentStatsInfo& info) {
  memset(&info, 0, sizeof(info));
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }

  RpcBase rpc(RpcMessageType::GetStatsRequest);
  sendMessage(parent_address_, rpc);
  RpcGetStatsReply* reply = receiveTypedRpc<RpcGetStatsReply, RpcMessageType::GetStatsReply>();
  info.memory_allocated_ = reply->memory_allocated_;
  info.num_connections_ = reply->num_connections_;
}

void HotRestartingChild::drainParentListeners() {
  if (restart_epoch_ > 0) {
    // No reply expected.
    RpcBase rpc(RpcMessageType::DrainListenersRequest);
    sendMessage(parent_address_, rpc);
  }
}

void HotRestartingChild::shutdownParentAdmin(HotRestart::ShutdownParentAdminInfo& info) {
  if (restart_epoch_ == 0) {
    return;
  }

  RpcBase rpc(RpcMessageType::ShutdownAdminRequest);
  sendMessage(parent_address_, rpc);
  RpcShutdownAdminReply* reply =
      receiveTypedRpc<RpcShutdownAdminReply, RpcMessageType::ShutdownAdminReply>();
  info.original_start_time_ = reply->original_start_time_;
}

void HotRestartingChild::terminateParent() {
  if (restart_epoch_ == 0 || parent_terminated_) {
    return;
  }

  RpcBase rpc(RpcMessageType::TerminateRequest);
  sendMessage(parent_address_, rpc);
  parent_terminated_ = true;
}

} // namespace Server
} // namespace Envoy
