#include "source/server/hot_restarting_parent.h"

#include "envoy/server/instance.h"

#include "source/common/memory/stats.h"
#include "source/common/network/utility.h"
#include "source/common/stats/stat_merger.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/server/listener_impl.h"

namespace Envoy {
namespace Server {

using HotRestartMessage = envoy::HotRestartMessage;

HotRestartingParent::HotRestartingParent(int base_id, int restart_epoch,
                                         const std::string& socket_path, mode_t socket_mode)
    : HotRestartingBase(base_id), restart_epoch_(restart_epoch) {
  child_address_ = createDomainSocketAddress(restart_epoch_ + 1, "child", socket_path, socket_mode);
  bindDomainSocket(restart_epoch_, "parent", socket_path, socket_mode);
}

void HotRestartingParent::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  socket_event_ = dispatcher.createFileEvent(
      myDomainSocket(),
      [this](uint32_t events) -> void {
        ASSERT(events == Event::FileReadyType::Read);
        onSocketEvent();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  internal_ = std::make_unique<Internal>(&server);
}

void HotRestartingParent::onSocketEvent() {
  std::unique_ptr<HotRestartMessage> wrapped_request;
  while ((wrapped_request = receiveHotRestartMessage(Blocking::No))) {
    if (wrapped_request->requestreply_case() == HotRestartMessage::kReply) {
      ENVOY_LOG(error, "child sent us a HotRestartMessage reply (we want requests); ignoring.");
      HotRestartMessage wrapped_reply;
      wrapped_reply.set_didnt_recognize_your_last_message(true);
      sendHotRestartMessage(child_address_, wrapped_reply);
      continue;
    }
    switch (wrapped_request->request().request_case()) {
    case HotRestartMessage::Request::kShutdownAdmin: {
      sendHotRestartMessage(child_address_, internal_->shutdownAdmin());
      break;
    }

    case HotRestartMessage::Request::kPassListenSocket: {
      sendHotRestartMessage(child_address_,
                            internal_->getListenSocketsForChild(wrapped_request->request()));
      break;
    }

    case HotRestartMessage::Request::kStats: {
      HotRestartMessage wrapped_reply;
      internal_->exportStatsToChild(wrapped_reply.mutable_reply()->mutable_stats());
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }

    case HotRestartMessage::Request::kDrainListeners: {
      internal_->drainListeners();
      break;
    }

    case HotRestartMessage::Request::kTerminate: {
      ENVOY_LOG(info, "shutting down due to child request");
      kill(getpid(), SIGTERM);
      break;
    }

    default: {
      ENVOY_LOG(error, "child sent us an unfamiliar type of HotRestartMessage; ignoring.");
      HotRestartMessage wrapped_reply;
      wrapped_reply.set_didnt_recognize_your_last_message(true);
      sendHotRestartMessage(child_address_, wrapped_reply);
      break;
    }
    }
  }
}

void HotRestartingParent::shutdown() { socket_event_.reset(); }

HotRestartingParent::Internal::Internal(Server::Instance* server) : server_(server) {
  Stats::Gauge& hot_restart_generation = hotRestartGeneration(server->stats());
  hot_restart_generation.inc();
}

HotRestartMessage HotRestartingParent::Internal::shutdownAdmin() {
  server_->shutdownAdmin();
  HotRestartMessage wrapped_reply;
  wrapped_reply.mutable_reply()->mutable_shutdown_admin()->set_original_start_time_unix_seconds(
      server_->startTimeFirstEpoch());
  wrapped_reply.mutable_reply()->mutable_shutdown_admin()->set_enable_reuse_port_default(
      server_->enableReusePortDefault());
  return wrapped_reply;
}

HotRestartMessage
HotRestartingParent::Internal::getListenSocketsForChild(const HotRestartMessage::Request& request) {
  HotRestartMessage wrapped_reply;
  wrapped_reply.mutable_reply()->mutable_pass_listen_socket()->set_fd(-1);
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::resolveUrl(request.pass_listen_socket().address());

  for (const auto& listener : server_->listenerManager().listeners()) {
    for (auto& socket_factory : listener.get().listenSocketFactories()) {
      if (*socket_factory->localAddress() == *addr && listener.get().bindToPort()) {
        StatusOr<Network::Socket::Type> socket_type =
            Network::Utility::socketTypeFromUrl(request.pass_listen_socket().address());
        // socketTypeFromUrl should return a valid value since resolveUrl returned a valid address.
        ASSERT(socket_type.ok());

        if (socket_factory->socketType() == *socket_type) {
          // worker_index() will default to 0 if not set which is the behavior before this field
          // was added. Thus, this should be safe for both roll forward and roll back.
          if (request.pass_listen_socket().worker_index() < server_->options().concurrency()) {
            wrapped_reply.mutable_reply()->mutable_pass_listen_socket()->set_fd(
                socket_factory->getListenSocket(request.pass_listen_socket().worker_index())
                    ->ioHandle()
                    .fdDoNotUse());
          }
          break;
        }
      }
    }
  }
  return wrapped_reply;
}

// TODO(fredlas) if there are enough stats for stat name length to become an issue, this current
// implementation can negate the benefit of symbolized stat names by periodically reaching the
// magnitude of memory usage that they are meant to avoid, since this map holds full-string
// names. The problem can be solved by splitting the export up over many chunks.
void HotRestartingParent::Internal::exportStatsToChild(HotRestartMessage::Reply::Stats* stats) {
  server_->stats().forEachSinkedGauge(nullptr, [this, stats](Stats::Gauge& gauge) mutable {
    if (gauge.used()) {
      const std::string name = gauge.name();
      (*stats->mutable_gauges())[name] = gauge.value();
      recordDynamics(stats, name, gauge.statName());
    }
  });

  server_->stats().forEachSinkedCounter(nullptr, [this, stats](Stats::Counter& counter) mutable {
    if (counter.used()) {
      // The hot restart parent is expected to have stopped its normal stat exporting (and so
      // latching) by the time it begins exporting to the hot restart child.
      uint64_t latched_value = counter.latch();
      if (latched_value > 0) {
        const std::string name = counter.name();
        (*stats->mutable_counter_deltas())[name] = latched_value;
        recordDynamics(stats, name, counter.statName());
      }
    }
  });
  stats->set_memory_allocated(Memory::Stats::totalCurrentlyAllocated());
  stats->set_num_connections(server_->listenerManager().numConnections());
}

void HotRestartingParent::Internal::recordDynamics(HotRestartMessage::Reply::Stats* stats,
                                                   const std::string& name,
                                                   Stats::StatName stat_name) {
  // Compute an array of spans describing which components of the stat name are
  // dynamic. This is needed so that when the child recovers the StatName, it
  // correlates with how the system generates those stats, with the same exact
  // components using a dynamic representation.
  //
  // See https://github.com/envoyproxy/envoy/issues/9874 for more details.
  Stats::DynamicSpans spans = server_->stats().symbolTable().getDynamicSpans(stat_name);

  // Convert that C++ structure (controlled by stat_merger.cc) into a protobuf
  // for serialization.
  if (!spans.empty()) {
    HotRestartMessage::Reply::RepeatedSpan spans_proto;
    for (const Stats::DynamicSpan& span : spans) {
      HotRestartMessage::Reply::Span* span_proto = spans_proto.add_spans();
      span_proto->set_first(span.first);
      span_proto->set_last(span.second);
    }
    (*stats->mutable_dynamics())[name] = spans_proto;
  }
}

void HotRestartingParent::Internal::drainListeners() { server_->drainListeners(); }

} // namespace Server
} // namespace Envoy
