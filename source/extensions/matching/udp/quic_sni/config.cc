#include "envoy/extensions/matching/inputs/quic_sni/v3/quic_sni_input.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "source/extensions/matching/udp/quic_sni/quic_sni_input.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Udp {
namespace QuicSni {

/**
 * Factory that creates the QuicSniInput matching input, registered as
 * "envoy.matching.inputs.quic_sni". No configuration is needed — the proto
 * message is empty (same pattern as DestinationIPInput).
 */
class QuicSniInputFactory : public ::Envoy::Matcher::DataInputFactory<Network::UdpMatchingData> {
public:
  std::string name() const override { return "envoy.matching.inputs.quic_sni"; }

  ::Envoy::Matcher::DataInputFactoryCb<Network::UdpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&,
                           ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<QuicSniInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::inputs::quic_sni::v3::QuicSniInput>();
  }
};

REGISTER_FACTORY(QuicSniInputFactory, ::Envoy::Matcher::DataInputFactory<Network::UdpMatchingData>);

} // namespace QuicSni
} // namespace Udp
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
