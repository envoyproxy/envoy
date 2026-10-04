#pragma once

#include <chrono>
#include <optional>

#include "envoy/network/filter.h"

#include "source/common/network/drain_close_util.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/network/reverse_tunnel/drain_aware_hcm/drain_aware_listener.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// HCM creates its HTTP/2 codec on the first request. Until then, a reverse tunnel speaks RPING
// and must be closed without creating a codec or emitting HTTP/2 frames when drain starts.
class DrainAwareReadFilter : public Network::ReadFilter, public Network::ConnectionCallbacks {
public:
  DrainAwareReadFilter(const Network::DrainDecision& drain_decision,
                       Server::Configuration::ServerFactoryContext& server_context,
                       Network::DrainDirection drain_direction)
      : drain_decision_(drain_decision), server_context_(server_context),
        drain_direction_(drain_direction) {}

  ~DrainAwareReadFilter() override {
    if (connection_ != nullptr) {
      connection_->removeConnectionCallbacks(*this);
      drain_check_timer_->disableTimer();
    }
  }

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    connection_ = &callbacks.connection();
    drain_type_ = Network::listenerDrainType(*connection_);
    drain_check_timer_ = connection_->dispatcher().createTimer([this]() { checkForDrain(); });
    // Registration can replay a drain event. Defer closing until all filters are initialized.
    connection_->addConnectionCallbacks(*this);
  }

  Network::FilterStatus onNewConnection() override {
    initialized_ = true;
    checkForDrain();
    return connection_->state() == Network::Connection::State::Open
               ? Network::FilterStatus::Continue
               : Network::FilterStatus::StopIteration;
  }

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    if (!http_started_) {
      checkForDrain();
      if (connection_->state() != Network::Connection::State::Open) {
        return Network::FilterStatus::StopIteration;
      }
      // The next filter creates the codec synchronously. Its drain observer takes over, including
      // replay of any drain event received while this connection was idle.
      http_started_ = true;
      drain_check_timer_->disableTimer();
    }
    return Network::FilterStatus::Continue;
  }

  // Network::ConnectionCallbacks
  void onDrain(Network::ConnectionDrainEvent drain_event) override {
    if (!connection_drain_event_.has_value()) {
      connection_drain_event_ = drain_event;
    }
    if (initialized_ && use_connection_event_drain_ && !http_started_ &&
        connection_->state() == Network::Connection::State::Open) {
      drain_check_timer_->enableTimer(std::chrono::milliseconds::zero());
    }
  }
  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      drain_check_timer_->disableTimer();
    }
  }
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void checkForDrain() {
    if (http_started_ || connection_->state() != Network::Connection::State::Open) {
      return;
    }
    if (use_connection_event_drain_ && connection_drain_event_.has_value()) {
      stopInitiatingReverseConnections(*connection_);
    }
    const bool draining =
        use_connection_event_drain_
            ? Network::shouldDrainClose(server_context_, drain_type_, connection_drain_event_)
            : drain_decision_.drainClose(drain_direction_);
    if (draining) {
      connection_->close(Network::ConnectionCloseType::NoFlush, "reverse_tunnel_listener_draining");
      return;
    }
    drain_check_timer_->enableTimer(std::chrono::milliseconds(100));
  }

  const Network::DrainDecision& drain_decision_;
  Server::Configuration::ServerFactoryContext& server_context_;
  const Network::DrainDirection drain_direction_;
  Network::Connection* connection_{nullptr};
  Event::TimerPtr drain_check_timer_;
  envoy::config::listener::v3::Listener::DrainType drain_type_{
      envoy::config::listener::v3::Listener::DEFAULT};
  std::optional<Network::ConnectionDrainEvent> connection_drain_event_;
  const bool use_connection_event_drain_{
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_connection_event_drain")};
  bool initialized_{false};
  bool http_started_{false};
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
