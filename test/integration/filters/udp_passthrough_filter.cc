#include <memory>

#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
class UdpPassthroughFilter : public Network::UdpListenerReadFilter {
public:
  UdpPassthroughFilter(Network::UdpReadFilterCallbacks& callbacks)
      : UdpListenerReadFilter(callbacks) {}

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData&) override {
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    return Network::FilterStatus::Continue;
  }
};

class UdpPassthroughFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager,
              Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpPassthroughFilter>(callbacks));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.filters.udp_listener.passthrough"; }
};

static Registry::RegisterFactory<UdpPassthroughFilterConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    registry_;
} // namespace Envoy
