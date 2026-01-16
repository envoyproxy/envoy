#pragma once

#include "envoy/network/connection.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/common/set_filter_state/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SetFilterState {

class SetFilterState : public Network::ReadFilter,
                       public Network::ConnectionCallbacks,
                       Logger::Loggable<Logger::Id::filter> {
public:
  SetFilterState(Filters::Common::SetFilterState::ConfigSharedPtr on_new_connection,
                 Filters::Common::SetFilterState::ConfigSharedPtr on_downstream_tls_handshake)
      : on_new_connection_(std::move(on_new_connection)),
        on_downstream_tls_handshake_(std::move(on_downstream_tls_handshake)) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    if (on_downstream_tls_handshake_ != nullptr) {
      if (read_callbacks_->connection().ssl() != nullptr) {
        waiting_for_downstream_tls_handshake_ = true;
        read_callbacks_->connection().addConnectionCallbacks(*this);
      } else {
        // Mirror tcp_proxy: when the downstream connection is not TLS, treat the TLS-handshake
        // hook as immediate.
        apply_downstream_tls_handshake_on_new_connection_ = true;
      }
    }
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void onDownstreamTlsHandshake();

  const Filters::Common::SetFilterState::ConfigSharedPtr on_new_connection_;
  const Filters::Common::SetFilterState::ConfigSharedPtr on_downstream_tls_handshake_;
  Network::ReadFilterCallbacks* read_callbacks_{};
  bool waiting_for_downstream_tls_handshake_{false};
  bool apply_downstream_tls_handshake_on_new_connection_{false};
  bool downstream_tls_handshake_{false};
};

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
