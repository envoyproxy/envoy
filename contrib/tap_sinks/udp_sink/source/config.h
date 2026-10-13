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
  createSinkPtr(const Protobuf::Message& config,
                Server::Configuration::GenericFactoryContext& tsf_context) override;
};

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
