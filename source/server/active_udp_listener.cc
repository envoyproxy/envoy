#include "source/server/active_udp_listener.h"

#include "envoy/network/exception.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
ActiveUdpListenerBase::ActiveUdpListenerBase(uint32_t worker_index, uint32_t concurrency,
                                             Network::UdpConnectionHandler& parent,
                                             Network::Socket& listen_socket,
                                             Network::UdpListenerPtr&& listener,
                                             Network::ListenerConfig* config)
    : ActiveListenerImplBase(parent, config), worker_index_(worker_index),
      concurrency_(concurrency), parent_(parent), listen_socket_(listen_socket),
      udp_listener_(std::move(listener)),
      udp_stats_({ALL_UDP_LISTENER_STATS(POOL_COUNTER_PREFIX(config->listenerScope(), "udp"))}),
      udp_listener_worker_router_(config_->udpListenerConfig()->listenerWorkerRouter(
          *listen_socket.connectionInfoProvider().localAddress())) {
  ASSERT(worker_index_ < concurrency_);
  udp_listener_worker_router_.registerWorkerForListener(*this);
}

ActiveUdpListenerBase::~ActiveUdpListenerBase() {
  udp_listener_worker_router_.unregisterWorkerForListener(*this);
}

void ActiveUdpListenerBase::post(Network::UdpRecvData&& data) {
  ASSERT(!udp_listener_->dispatcher().isThreadSafe(),
         "Shouldn't be posting if thread safe; use onWorkerData() instead.");

  auto address = listen_socket_.connectionInfoProvider().localAddress();
  udp_listener_->dispatcher().post([data = std::move(data), tag = config_->listenerTag(),
                                    &parent = parent_, address]() mutable {
    Network::UdpListenerCallbacksOptRef listener = parent.getUdpListenerCallbacks(tag, *address);
    if (listener.has_value()) {
      listener->get().onDataWorker(std::move(data));
    }
  });
}

void ActiveUdpListenerBase::onData(Network::UdpRecvData&& data) {
  uint32_t dest = worker_index_;

  // For concurrency == 1, the packet will always go to the current worker.
  if (concurrency_ > 1) {
    dest = destination(data);
    ASSERT(dest < concurrency_);
  }

  if (dest == worker_index_) {
    onDataWorker(std::move(data));
  } else {
    udp_listener_worker_router_.deliver(dest, std::move(data));
  }
}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, *listen_socket_ptr, listen_socket_ptr,
                           dispatcher, config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, listen_socket,
                           std::make_unique<Network::UdpListenerImpl>(
                               dispatcher, listen_socket_ptr, *this, dispatcher.timeSource(),
                               config.udpListenerConfig()->config().downstream_socket_config()),
                           config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::UdpConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::UdpListenerPtr&& listener,
                                           Network::ListenerConfig& config)
    : ActiveUdpListenerBase(worker_index, concurrency, parent, listen_socket, std::move(listener),
                            &config),
      flow_sweep_timer_(udp_listener_->dispatcher().createTimer([this] { sweepIdleFlows(); })),
      flow_idle_timeout_(std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
          config.udpListenerConfig()->config(), flow_idle_timeout, 60000))) {
  // Create the filter chain on creating a new udp listener.
  config_->filterChainFactory().createUdpListenerFilterChain(*this, *this);

  // If filter is nullptr warn that we will be dropping packets. This is an edge case and should
  // only happen due to a bad factory. It's not worth adding per-worker error handling for this.
  if (read_filters_.empty()) {
    ENVOY_LOG(warn, "UDP listener has no filters. Packets will be dropped.");
  }

  // Create udp_packet_writer
  udp_packet_writer_ = config_->udpListenerConfig()->packetWriterFactory().createUdpPacketWriter(
      listen_socket_.ioHandle(), config.listenerScope(), /*dispatcher=*/udp_listener_->dispatcher(),
      /*on_can_write_cb=*/[]() {});
}

void ActiveRawUdpListener::onDataWorker(Network::UdpRecvData&& data) {
  if (udp_listener_ == nullptr) {
    // Already shut down.
    return;
  }

  const MonotonicTime now = udp_listener_->dispatcher().approximateMonotonicTime();
  if (non_dispatched_udp_packet_handler_.has_value()) {
    auto flow_it = flow_last_activity_.find(data.addresses_);
    if (flow_it == flow_last_activity_.end()) {
      // Draining for hot restart: this flow isn't ours, hand it to the child instance.
      non_dispatched_udp_packet_handler_->handle(worker_index_, std::move(data));
      return;
    }
    flow_it->second = now;
  } else {
    auto [flow_it, inserted] = flow_last_activity_.try_emplace(data.addresses_, now);
    if (inserted) {
      if (flow_last_activity_.size() == 1) {
        flow_sweep_timer_->enableTimer(flow_idle_timeout_);
      }
    } else {
      flow_it->second = now;
    }
  }

  for (auto& read_filter : read_filters_) {
    Network::FilterStatus status = read_filter->onData(data);
    if (status == Network::FilterStatus::StopIteration) {
      return;
    }
  }
}

void ActiveRawUdpListener::sweepIdleFlows() {
  const MonotonicTime cutoff =
      udp_listener_->dispatcher().approximateMonotonicTime() - flow_idle_timeout_;
  for (auto it = flow_last_activity_.begin(); it != flow_last_activity_.end();) {
    if (it->second < cutoff) {
      flow_last_activity_.erase(it++);
    } else {
      ++it;
    }
  }
  if (!flow_last_activity_.empty()) {
    flow_sweep_timer_->enableTimer(flow_idle_timeout_);
  }
}

void ActiveRawUdpListener::onReadReady() {}

void ActiveRawUdpListener::onWriteReady(const Network::Socket&) {
  // TODO(sumukhs): This is not used now. When write filters are implemented, this is a
  // trigger to invoke the on write ready API on the filters which is when they can write
  // data.

  // Clear write_blocked_ status for udpPacketWriter.
  udp_packet_writer_->setWritable();
}

void ActiveRawUdpListener::onReceiveError(Api::IoError::IoErrorCode error_code) {
  for (auto& read_filter : read_filters_) {
    Network::FilterStatus status = read_filter->onReceiveError(error_code);
    if (status == Network::FilterStatus::StopIteration) {
      return;
    }
  }
}

void ActiveRawUdpListener::addReadFilter(Network::UdpListenerReadFilterPtr&& filter) {
  read_filters_.emplace_back(std::move(filter));
}

Network::UdpListener& ActiveRawUdpListener::udpListener() { return *udp_listener_; }

} // namespace Server
} // namespace Envoy
