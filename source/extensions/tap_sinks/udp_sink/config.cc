#include "source/extensions/tap_sinks/udp_sink/config.h"

#include "envoy/extensions/tap_sinks/udp_sink/v3/udp_sink.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/tap_sinks/udp_sink/udp_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

TapCommon::SinkPtr UdpTapSinkFactory::createTransportSinkPtr(
    const Protobuf::Message& config,
    Server::Configuration::TransportSocketFactoryContext& tsf_context) {
  ENVOY_LOG_MISC(trace, "{}: Create UDP sink in transport context", __func__);
  return std::make_unique<UdpTapSink>(
      MessageUtil::downcastAndValidate<const envoy::extensions::tap_sinks::udp_sink::v3::UdpSink&>(
          config, tsf_context.messageValidationVisitor()));
}

TapCommon::SinkPtr
UdpTapSinkFactory::createHttpSinkPtr(const Protobuf::Message& config,
                                     Server::Configuration::FactoryContext& http_context) {
  ENVOY_LOG_MISC(trace, "{}: Create UDP sink in http context", __func__);
  return std::make_unique<UdpTapSink>(
      MessageUtil::downcastAndValidate<const envoy::extensions::tap_sinks::udp_sink::v3::UdpSink&>(
          config, http_context.serverFactoryContext()
                      .messageValidationContext()
                      .staticValidationVisitor()));
}

REGISTER_FACTORY(UdpTapSinkFactory, TapCommon::TapSinkFactory);

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
