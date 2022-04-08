#include "source/common/network/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <>
Matcher::DataInputGetResult DestinationIPInput<MatchingData>::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

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
DestinationPortInput<MatchingData>::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
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
Matcher::DataInputGetResult SourceIPInput<MatchingData>::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
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
Matcher::DataInputGetResult SourcePortInput<MatchingData>::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
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

Matcher::DataInputGetResult DirectSourceIPInput::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().directRemoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

Matcher::DataInputGetResult SourceTypeInput::get(const MatchingData& data) const {
  const bool is_local_connection = Network::Utility::isSameIpOrLoopback(data.socket());
  if (is_local_connection) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, "local"};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

Matcher::DataInputGetResult ServerNameInput::get(const MatchingData& data) const {
  const auto server_name = data.socket().requestedServerName();
  if (!server_name.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(server_name)};
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
