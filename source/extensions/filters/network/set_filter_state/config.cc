#include "source/extensions/filters/network/set_filter_state/config.h"

#include <string>

#include "envoy/extensions/filters/network/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/network/set_filter_state/v3/set_filter_state.pb.validate.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SetFilterState {

Network::FilterStatus SetFilterState::onNewConnection() {
  if (on_new_connection_ != nullptr) {
    on_new_connection_->updateFilterState({}, read_callbacks_->connection().streamInfo());
  }
  if (apply_downstream_tls_handshake_on_new_connection_ && !downstream_tls_handshake_) {
    onDownstreamTlsHandshake();
  }
  return Network::FilterStatus::Continue;
}

void SetFilterState::onEvent(Network::ConnectionEvent event) {
  // For SSL connections the Connected event is raised after the downstream TLS handshake completes.
  // Mirror tcp_proxy's behavior: only run the TLS hook for downstream TLS connections and only
  // once.
  if (event == Network::ConnectionEvent::Connected && waiting_for_downstream_tls_handshake_ &&
      !downstream_tls_handshake_) {
    onDownstreamTlsHandshake();
    return;
  }

  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    // No further work to do after connection teardown starts.
    waiting_for_downstream_tls_handshake_ = false;
  }
}

void SetFilterState::onDownstreamTlsHandshake() {
  downstream_tls_handshake_ = true;
  waiting_for_downstream_tls_handshake_ = false;
  apply_downstream_tls_handshake_on_new_connection_ = false;
  if (on_downstream_tls_handshake_ != nullptr) {
    on_downstream_tls_handshake_->updateFilterState({}, read_callbacks_->connection().streamInfo());
  }
}

/**
 * Config registration for the filter. @see NamedNetworkFilterConfigFactory.
 */
class SetFilterStateConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::set_filter_state::v3::Config> {
public:
  SetFilterStateConfigFactory() : FactoryBase("envoy.filters.network.set_filter_state") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::set_filter_state::v3::Config& proto_config,
      Server::Configuration::FactoryContext& context) override {
    Filters::Common::SetFilterState::ConfigSharedPtr on_new_connection_config;
    if (!proto_config.on_new_connection().empty()) {
      on_new_connection_config = std::make_shared<Filters::Common::SetFilterState::Config>(
          proto_config.on_new_connection(), StreamInfo::FilterState::LifeSpan::Connection, context);
    }

    Filters::Common::SetFilterState::ConfigSharedPtr on_downstream_tls_handshake_config;
    if (!proto_config.on_downstream_tls_handshake().empty()) {
      on_downstream_tls_handshake_config =
          std::make_shared<Filters::Common::SetFilterState::Config>(
              proto_config.on_downstream_tls_handshake(),
              StreamInfo::FilterState::LifeSpan::Connection, context);
    }

    return [on_new_connection_config,
            on_downstream_tls_handshake_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<SetFilterState>(
          on_new_connection_config, on_downstream_tls_handshake_config));
    };
  }

  bool isTerminalFilterByProtoTyped(
      const envoy::extensions::filters::network::set_filter_state::v3::Config&,
      Server::Configuration::ServerFactoryContext&) override {
    return false;
  }
};

REGISTER_FACTORY(SetFilterStateConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SetFilterState
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
