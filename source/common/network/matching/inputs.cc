#include "source/common/network/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <>
Matcher::DataInputGetResult
DestinationIPInput<UdpMatchingData>::get(const UdpMatchingData& data) const {
  const auto& address = data.localAddress();
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address.ip()->addressAsString()};
}

template <>
Matcher::DataInputGetResult
DestinationPortInput<UdpMatchingData>::get(const UdpMatchingData& data) const {
  const auto& address = data.localAddress();
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address.ip()->port())};
}

template <>
Matcher::DataInputGetResult SourceIPInput<UdpMatchingData>::get(const UdpMatchingData& data) const {
  const auto& address = data.remoteAddress();
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address.ip()->addressAsString()};
}

template <>
Matcher::DataInputGetResult
SourcePortInput<UdpMatchingData>::get(const UdpMatchingData& data) const {
  const auto& address = data.remoteAddress();
  if (address.type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address.ip()->port())};
}

Matcher::DataInputGetResult SourceTypeInput::get(const MatchingData& data) const {
  const bool is_local_connection = Network::Utility::isSameIpOrLoopback(data.socket());
  if (is_local_connection) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, "local"};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

Matcher::DataInputGetResult TransportProtocolInput::get(const MatchingData& data) const {
  const auto transport_protocol = data.socket().detectedTransportProtocol();
  if (!transport_protocol.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(transport_protocol)};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

Matcher::DataInputGetResult ApplicationProtocolInput::get(const MatchingData& data) const {
  const auto& protocols = data.socket().requestedApplicationProtocols();
  if (!protocols.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            absl::StrCat("'", absl::StrJoin(protocols, "','"), "'")};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

using DestinationIPInputFactory = DestinationIPInputBaseFactory<MatchingData>;
using UdpDestinationIPInputFactory = DestinationIPInputBaseFactory<UdpMatchingData>;
using DestinationPortInputFactory = DestinationPortInputBaseFactory<MatchingData>;
using UdpDestinationPortInputFactory = DestinationPortInputBaseFactory<UdpMatchingData>;
using SourceIPInputFactory = SourceIPInputBaseFactory<MatchingData>;
using UdpSourceIPInputFactory = SourceIPInputBaseFactory<UdpMatchingData>;
using SourcePortInputFactory = SourcePortInputBaseFactory<MatchingData>;
using UdpSourcePortInputFactory = SourcePortInputBaseFactory<UdpMatchingData>;
using DirectSourceIPInputFactory = DirectSourceIPInputBaseFactory<MatchingData>;
using ServerNameInputFactory = ServerNameBaseFactory<MatchingData>;

REGISTER_FACTORY(DestinationIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(UdpDestinationIPInputFactory, Matcher::DataInputFactory<UdpMatchingData>);
REGISTER_FACTORY(DestinationPortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(UdpDestinationPortInputFactory, Matcher::DataInputFactory<UdpMatchingData>);
REGISTER_FACTORY(SourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(UdpSourceIPInputFactory, Matcher::DataInputFactory<UdpMatchingData>);
REGISTER_FACTORY(SourcePortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(UdpSourcePortInputFactory, Matcher::DataInputFactory<UdpMatchingData>);
REGISTER_FACTORY(DirectSourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourceTypeInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(ServerNameInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(TransportProtocolInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(ApplicationProtocolInputFactory, Matcher::DataInputFactory<MatchingData>);

} // namespace Matching
} // namespace Network
} // namespace Envoy
