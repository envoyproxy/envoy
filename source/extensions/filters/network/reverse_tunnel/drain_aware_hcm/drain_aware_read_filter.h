#pragma once

#include <chrono>
#include <optional>

#include "envoy/network/filter.h"

#include "source/common/network/drain_close_util.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/downstream_reverse_connection_io_handle.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// HCM creates its HTTP/2 codec on the first request. Until then, a reverse tunnel speaks RPING
// and must be closed without creating a codec or emitting HTTP/2 frames when drain starts.
class DrainAwareReadFilter : public Network::ReadFilter, public Network::ConnectionCallbacks {
public:
  explicit DrainAwareReadFilter(Server::Configuration::ServerFactoryContext& server_context)
      : server_context_(server_context) {}

  ~DrainAwareReadFilter() override {
    if (tunnel_iohandle_ != nullptr) {
      connection_->removeConnectionCallbacks(*this);
      drain_check_timer_->disableTimer();
    }
  }

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    connection_ = &callbacks.connection();
    if (connection_->getSocket() != nullptr) {
      tunnel_iohandle_ =
          dynamic_cast<Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle*>(
              &connection_->getSocket()->ioHandle());
    }
    // The drain-aware HCM is also usable on normal listeners. Leave their idle clients to HCM.
    if (tunnel_iohandle_ == nullptr) {
      return;
    }
    drain_type_ = Network::listenerDrainType(*connection_);
    drain_check_timer_ = connection_->dispatcher().createTimer([this]() { closeIdleTunnel(); });
    // Registration can replay a drain event. Defer closing until all filters are initialized.
    connection_->addConnectionCallbacks(*this);
  }

  Network::FilterStatus onNewConnection() override {
    initialized_ = true;
    scheduleDrain();
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    if (tunnel_iohandle_ == nullptr) {
      return Network::FilterStatus::Continue;
    }
    if (connection_->state() != Network::Connection::State::Open) {
      return Network::FilterStatus::StopIteration;
    }
    if (!http_started_) {
      // The next filter creates the codec synchronously. Its drain observer takes over, including
      // replay of any drain event received while this connection was idle.
      http_started_ = true;
      drain_check_timer_->disableTimer();
    }
    return Network::FilterStatus::Continue;
  }

  // Network::ConnectionCallbacks
  void onDrain(Network::ConnectionDrainEvent drain_event) override {
    if (connection_drain_event_.has_value()) {
      return;
    }
    connection_drain_event_ = drain_event;
    scheduleDrain();
  }
  void onEvent(Network::ConnectionEvent event) override {
    if (drain_check_timer_ != nullptr && (event == Network::ConnectionEvent::LocalClose ||
                                          event == Network::ConnectionEvent::RemoteClose)) {
      drain_check_timer_->disableTimer();
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void scheduleDrain() {
    if (!initialized_ || tunnel_iohandle_ == nullptr || http_started_ || drain_scheduled_ ||
        !connection_drain_event_.has_value() ||
        connection_->state() != Network::Connection::State::Open) {
      return;
    }
    drain_scheduled_ = true;
    std::chrono::milliseconds delay{0};
    const bool health_failed = drain_type_ == envoy::config::listener::v3::Listener::DEFAULT &&
                               server_context_.healthCheckFailed();
    const std::chrono::milliseconds drain_time = server_context_.options().drainTime();
    if (!health_failed && connection_drain_event_->strategy == Server::DrainStrategy::Gradual &&
        drain_time.count() > 0) {
      // Sample once over the original drain window. Repeated probabilistic polling would close
      // almost every idle tunnel near the start of a gradual drain.
      const std::chrono::milliseconds close_after{server_context_.api().randomGenerator().random() %
                                                  drain_time.count()};
      const MonotonicTime now = server_context_.timeSource().monotonicTime();
      const auto elapsed = now <= connection_drain_event_->start_time
                               ? std::chrono::milliseconds{0}
                               : std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now - connection_drain_event_->start_time);
      delay = close_after > elapsed ? close_after - elapsed : std::chrono::milliseconds{0};
    }
    // Even an immediate/replayed drain waits until every filter has been initialized.
    drain_check_timer_->enableTimer(delay);
  }

  void closeIdleTunnel() {
    if (http_started_ || connection_->state() != Network::Connection::State::Open) {
      return;
    }
    // In-place filter-chain updates must refill the idle pool before the maintenance tick. A
    // whole-listener drain has already stopped the parent, so the callback cannot replenish it.
    tunnel_iohandle_->markTunnelDrainingAndDialReplacement();
    connection_->close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining");
  }

  Server::Configuration::ServerFactoryContext& server_context_;
  Network::Connection* connection_{nullptr};
  Bootstrap::ReverseConnection::DownstreamReverseConnectionIOHandle* tunnel_iohandle_{nullptr};
  Event::TimerPtr drain_check_timer_;
  envoy::config::listener::v3::Listener::DrainType drain_type_{
      envoy::config::listener::v3::Listener::DEFAULT};
  std::optional<Network::ConnectionDrainEvent> connection_drain_event_;
  bool initialized_{false};
  bool http_started_{false};
  bool drain_scheduled_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
