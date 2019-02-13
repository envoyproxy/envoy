#include "server/hot_restarting_parent.h"

#include "envoy/server/instance.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::admin::v2alpha::HotRestartMessage;

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

void HotRestartingParent::onSocketEvent() {
  std::unique_ptr<HotRestartMessage> wrapped_request;
  while ((wrapped_request = receiveHotRestartMessage(/*blocking=*/false))) {
    switch (wrapped_request->request_case()) {
    case HotRestartMessage::kShutdownAdminRequest: {
      server_->shutdownAdmin();
      HotRestartMessage wrapped_reply;
      wrapped_reply.mutable_shutdown_admin_reply()->set_original_start_time(
          server_->startTimeFirstEpoch());
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::kPassListenSocketRequest: {
      HotRestartMessage wrapped_reply;
      wrapped_reply.mutable_pass_listen_socket_reply()->set_fd(-1);
      Network::Address::InstanceConstSharedPtr addr =
          Network::Utility::resolveUrl(wrapped_request->pass_listen_socket_request().address());
      for (const auto& listener : server_->listenerManager().listeners()) {
        if (*listener.get().socket().localAddress() == *addr) {
          wrapped_reply.mutable_pass_listen_socket_reply()->set_fd(
              listener.get().socket().ioHandle().fd());
          break;
        }
      }
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::kStatsRequest: {
      HotRestart::GetParentStatsInfo info;
      server_->getParentStats(info);
      HotRestartMessage wrapped_reply;
      wrapped_reply.mutable_stats_reply()->set_memory_allocated(info.memory_allocated_);
      wrapped_reply.mutable_stats_reply()->set_num_connections(info.num_connections_);
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::kDrainListenersRequest: {
      server_->drainListeners();
      break;
    }

    case HotRestartMessage::kTerminateRequest: {
      ENVOY_LOG(info, "shutting down due to child request");
      kill(getpid(), SIGTERM);
      break;
    }

    default: {
      HotRestartMessage wrapped_reply;
      wrapped_reply.set_didnt_recognize_your_last_message(true);
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }
    }
  }
}

void HotRestartingParent::shutdown() { socket_event_.reset(); }

} // namespace Server
} // namespace Envoy
