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
  // Network::UdpListenerReadFilter
  void onData(Network::UdpRecvData& data) override;
  void setCallbacks(Network::UdpReadFilterCallbacks& callbacks) override;

private:
  Network::UdpReadFilterCallbacks* read_callbacks_{};
};

void UdpListenerEchoFilter::setCallbacks(Network::UdpReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

void UdpListenerEchoFilter::onData(Network::UdpRecvData& data) {
  ENVOY_LOG(trace, "UdpEchoFilter: Got {} bytes from {}", data.buffer_->length(),
            data.peer_address_->asString());

  // send back the received data
  Network::UdpSendData send_data{data.peer_address_, *data.buffer_};
  read_callbacks_->udpListener().send(send_data);

  return;
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
    return [](Network::UdpListenerFilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpListenerEchoFilter>());
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
