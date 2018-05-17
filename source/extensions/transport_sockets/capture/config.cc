#include "extensions/transport_sockets/capture/config.h"

#include "envoy/config/transport_socket/capture/v2alpha/capture.pb.h"
#include "envoy/config/transport_socket/capture/v2alpha/capture.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/transport_sockets/capture/capture.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Capture {

Network::TransportSocketFactoryPtr UpstreamCaptureSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& context) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::config::transport_socket::capture::v2alpha::Capture&>(message);
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::UpstreamTransportSocketConfigFactory>(
      outer_config.transport_socket().name());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), inner_config_factory);
  auto inner_transport_factory =
      inner_config_factory.createTransportSocketFactory(*inner_factory_config, context);
  return std::make_unique<CaptureSocketFactory>(outer_config.file_sink().path_prefix(),
                                                outer_config.file_sink().format(),
                                                std::move(inner_transport_factory));
}

Network::TransportSocketFactoryPtr
DownstreamCaptureSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext& context,
    const std::vector<std::string>& server_names) {
  const auto& outer_config = MessageUtil::downcastAndValidate<
      const envoy::config::transport_socket::capture::v2alpha::Capture&>(message);
  auto& inner_config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      outer_config.transport_socket().name());
  ProtobufTypes::MessagePtr inner_factory_config = Config::Utility::translateToFactoryConfig(
      outer_config.transport_socket(), inner_config_factory);
  auto inner_transport_factory = inner_config_factory.createTransportSocketFactory(
      *inner_factory_config, context, server_names);
  return std::make_unique<CaptureSocketFactory>(outer_config.file_sink().path_prefix(),
                                                outer_config.file_sink().format(),
                                                std::move(inner_transport_factory));
}

ProtobufTypes::MessagePtr CaptureSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::transport_socket::capture::v2alpha::Capture>();
}

static Registry::RegisterFactory<UpstreamCaptureSocketConfigFactory,
                                 Server::Configuration::UpstreamTransportSocketConfigFactory>
    upstream_registered_;

static Registry::RegisterFactory<DownstreamCaptureSocketConfigFactory,
                                 Server::Configuration::DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace Capture
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
