#include "contrib/tap_sinks/udp_sink/source/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "contrib/envoy/extensions/tap_sinks/udp_sink/v3alpha/udp_sink.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

TapCommon::SinkPtr UdpTapSinkFactory::createTransportSinkPtr(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& tsf_context) {
  ENVOY_LOG_MISC(trace, "{}: Create UDP sink in transport context", __func__);
  return std::make_unique<UdpTapSink>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink&>(
          config, tsf_context.messageValidationVisitor()));
}

TapCommon::SinkPtr
UdpTapSinkFactory::createHttpSinkPtr(const Protobuf::Message& config,
                                     Server::Configuration::FactoryContext& http_context) {
  ENVOY_LOG_MISC(trace, "{}: Create UDP sink in http context", __func__);
  return std::make_unique<UdpTapSink>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink&>(
          config, http_context.messageValidationVisitor()));
}

REGISTER_FACTORY(UdpTapSinkFactory, TapCommon::TapSinkFactory);

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
