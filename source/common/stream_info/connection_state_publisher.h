#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Publishes explicitly selected information back to the client side of an internal connection.
 * Installed only on the server side, with connection lifetime and no upstream sharing. It must
 * never be inherited from the initiating request: separate pooled connections need separate
 * publishers. The weak reference avoids extending the lifetime of the client connection state.
 * Both sides of an internal connection run on the same dispatcher thread.
 */
class ConnectionStatePublisher : public FilterState::Object {
public:
  explicit ConnectionStatePublisher(const FilterStateSharedPtr& client_state)
      : client_state_(client_state) {}

  static constexpr absl::string_view key() {
    return "envoy.internal_listener.connection_state_publisher";
  }

  void publish(absl::string_view name, std::shared_ptr<FilterState::Object> value) const {
    if (auto state = client_state_.lock(); state != nullptr) {
      state->setData(name, std::move(value), FilterState::LifeSpan::Connection);
    }
  }

private:
  const std::weak_ptr<FilterState> client_state_;
};

} // namespace StreamInfo
} // namespace Envoy
