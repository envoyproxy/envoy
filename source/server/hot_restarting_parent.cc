#include "server/hot_restarting_parent.h"

#include "envoy/server/instance.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Server {

HotRestartingParent::HotRestartingParent(int base_id, int restart_epoch)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  child_address_ = createDomainSocketAddress(restart_epoch_ + 1, "child");
  bindDomainSocket(restart_epoch_, "parent");
}

void HotRestartingParent::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  socket_event_ =
      dispatcher.createFileEvent(my_domain_socket(),
                                 [this](uint32_t events) -> void {
                                   ASSERT(events == Event::FileReadyType::Read);
                                   onSocketEvent();
                                 },
                                 Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  server_ = &server;
}

void HotRestartingParent::onGetListenSocket(RpcGetListenSocketRequest& rpc) {
  RpcGetListenSocketReply reply;
  reply.fd_ = -1;

  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::resolveUrl(std::string(rpc.address_));
  for (const auto& listener : server_->listenerManager().listeners()) {
    if (*listener.get().socket().localAddress() == *addr) {
      reply.fd_ = listener.get().socket().ioHandle().fd();
      break;
    }
  }

  if (reply.fd_ == -1) {
    // In this case there is no fd to duplicate so we just send a normal message.
    sendMessage(child_address_, reply);
  } else {
    iovec iov[1];
    iov[0].iov_base = &reply;
    iov[0].iov_len = reply.length_;

    uint8_t control_buffer[CMSG_SPACE(sizeof(int))];
    memset(control_buffer, 0, CMSG_SPACE(sizeof(int)));

    msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_name = &child_address_;
    message.msg_namelen = sizeof(child_address_);
    message.msg_iov = iov;
    message.msg_iovlen = 1;
    message.msg_control = control_buffer;
    message.msg_controllen = CMSG_SPACE(sizeof(int));

    cmsghdr* control_message = CMSG_FIRSTHDR(&message);
    control_message->cmsg_level = SOL_SOCKET;
    control_message->cmsg_type = SCM_RIGHTS;
    control_message->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int*>(CMSG_DATA(control_message)) = reply.fd_;

    int rc = sendmsg(my_domain_socket(), &message, 0);
    RELEASE_ASSERT(rc != -1, "");
  }
}

void HotRestartingParent::onSocketEvent() {
  while (true) {
    RpcBase* base_message = receiveRpc(false);
    if (!base_message) {
      return;
    }

    switch (base_message->type_) {
    case RpcMessageType::ShutdownAdminRequest: {
      server_->shutdownAdmin();
      RpcShutdownAdminReply rpc;
      rpc.original_start_time_ = server_->startTimeFirstEpoch();
      sendMessage(child_address_, rpc);
      break;
    }

    case RpcMessageType::GetListenSocketRequest: {
      RpcGetListenSocketRequest* message =
          reinterpret_cast<RpcGetListenSocketRequest*>(base_message);
      onGetListenSocket(*message);
      break;
    }

    case RpcMessageType::GetStatsRequest: {
      HotRestart::GetParentStatsInfo info;
      server_->getParentStats(info);
      RpcGetStatsReply rpc;
      rpc.memory_allocated_ = info.memory_allocated_;
      rpc.num_connections_ = info.num_connections_;
      sendMessage(child_address_, rpc);
      break;
    }

    case RpcMessageType::DrainListenersRequest: {
      server_->drainListeners();
      break;
    }

    case RpcMessageType::TerminateRequest: {
      ENVOY_LOG(info, "shutting down due to child request");
      kill(getpid(), SIGTERM);
      break;
    }

    default: {
      RpcBase rpc(RpcMessageType::UnknownRequestReply);
      sendMessage(child_address_, rpc);
      break;
    }
    }
  }
}

void HotRestartingParent::shutdown() { socket_event_.reset(); }

} // namespace Server
} // namespace Envoy
