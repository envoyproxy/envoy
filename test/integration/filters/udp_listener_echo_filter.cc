#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {

class UdpListenerEchoFilter : public Network::UdpListenerReadFilter,
                              Logger::Loggable<Logger::Id::filter> {
public:
  UdpListenerEchoFilter(Network::UdpReadFilterCallbacks& callbacks);

  // Network::UdpListenerReadFilter
  void onData(Network::UdpRecvData& data) override;
};

UdpListenerEchoFilter::UdpListenerEchoFilter(Network::UdpReadFilterCallbacks& callbacks)
    : Network::UdpListenerReadFilter(callbacks) {}

void UdpListenerEchoFilter::onData(Network::UdpRecvData& data) {
  ENVOY_LOG(trace, "UdpEchoFilter: Got {} bytes from {} on {}", data.buffer_->length(),
            data.peer_address_->asString(), data.local_address_->asString());

  // send back the received data
  Network::UdpSendData send_data{data.local_address_->ip(), *data.peer_address_, *data.buffer_};
  auto send_result = read_callbacks_->udpListener().send(send_data);

  ASSERT(send_result.ok());
}

/**
 * Config registration for the echo filter. @see NamedNetworkFilterConfigFactory.
 */
class UdpListenerEchoFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::ListenerFactoryContext&) override {
    return [](Network::UdpListenerFilterManager& filter_manager,
              Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpListenerEchoFilter>(callbacks));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return "envoy.listener.udpecho"; }
};

// perform static registration
static Registry::RegisterFactory<UdpListenerEchoFilterConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    register_;

} // namespace Envoy
