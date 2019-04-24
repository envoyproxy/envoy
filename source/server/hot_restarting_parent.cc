#include "server/hot_restarting_parent.h"

#include "envoy/server/instance.h"

#include "common/memory/stats.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

HotRestartingParent::HotRestartingParent(int base_id, int restart_epoch)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  child_address_ = createDomainSocketAddress(restart_epoch_ + 1, "child");
  bindDomainSocket(restart_epoch_, "parent");
}

void HotRestartingParent::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  socket_event_ = dispatcher.createFileEvent(
      my_domain_socket(),
      [this](uint32_t events) -> void {
        ASSERT(events == Event::FileReadyType::Read);
        onSocketEvent();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  server_ = &server;
}

void HotRestartingParent::onSocketEvent() {
  std::unique_ptr<HotRestartMessage> wrapped_request;
  while ((wrapped_request = receiveHotRestartMessage(Blocking::No))) {
    if (wrapped_request->requestreply_case() == HotRestartMessage::kReply) {
      ENVOY_LOG(error, "child sent us a reply type HotRestartMessage; ignoring.");
      HotRestartMessage wrapped_reply;
      wrapped_reply.set_didnt_recognize_your_last_message(true);
      sendHotRestartMessage(child_address_, wrapped_reply);
      continue;
    }
    switch (wrapped_request->request().request_case()) {
    case HotRestartMessage::Request::kShutdownAdmin: {
      server_->shutdownAdmin();
      HotRestartMessage wrapped_reply;
      wrapped_reply.mutable_reply()->mutable_shutdown_admin()->set_original_start_time_unix_seconds(
          server_->startTimeFirstEpoch());
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::Request::kPassListenSocket: {
      HotRestartMessage wrapped_reply;
      wrapped_reply.mutable_reply()->mutable_pass_listen_socket()->set_fd(-1);
      Network::Address::InstanceConstSharedPtr addr =
          Network::Utility::resolveUrl(wrapped_request->request().pass_listen_socket().address());
      for (const auto& listener : server_->listenerManager().listeners()) {
        if (*listener.get().socket().localAddress() == *addr) {
          wrapped_reply.mutable_reply()->mutable_pass_listen_socket()->set_fd(
              listener.get().socket().ioHandle().fd());
          break;
        }
      }
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::Request::kStats: {
      HotRestartMessage wrapped_reply;
      exportStatsToChild(wrapped_reply.mutable_reply()->mutable_stats());
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::Request::kDrainListeners: {
      server_->drainListeners();
      break;
    }

    case HotRestartMessage::Request::kTerminate: {
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

void HotRestartingParent::exportStatsToChild(HotRestartMessage::Reply::Stats* stats) {
  for (const auto& gauge : server_->stats().gauges()) {
    (*stats->mutable_gauges())[gauge->name()] = gauge->value();
  }
  for (const auto& counter : server_->stats().counters()) {
    (*stats->mutable_counters())[counter->name()] = counter->value();
  }
  stats->set_memory_allocated(Memory::Stats::totalCurrentlyAllocated());
  stats->set_num_connections(server_->listenerManager().numConnections());
}

void HotRestartingParent::shutdown() { socket_event_.reset(); }

} // namespace Server
} // namespace Envoy
