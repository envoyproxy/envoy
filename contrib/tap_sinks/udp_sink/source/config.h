#pragma once

#include "source/extensions/common/tap/tap.h"

#include "contrib/tap_sinks/udp_sink/source/udp_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

namespace TapCommon = Extensions::Common::Tap;

class UdpTapSinkFactory : public TapCommon::TapSinkFactory {
public:
  ~UdpTapSinkFactory() override = default;
  std::string name() const override { return "envoy.tap.sinks.udp"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink>();
  }
  TapCommon::SinkPtr
  createHttpSinkPtr(const Protobuf::Message& config,
                    Server::Configuration::FactoryContext& http_context) override;
  TapCommon::SinkPtr createTransportSinkPtr(
      const Protobuf::Message& config,
      Server::Configuration::TransportSocketFactoryContext& tsf_context) override;
};

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
