#include "source/common/network/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {
namespace Matching {

Matcher::DataInputGetResult DestinationIPInput::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

Matcher::DataInputGetResult DestinationPortInput::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
}

Matcher::DataInputGetResult SourceIPInput::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

Matcher::DataInputGetResult SourcePortInput::get(const MatchingData& data) const {
  const auto& address = data.socket().connectionInfoProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
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
  if (!data.socket().requestedServerName().empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.socket().requestedServerName())};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

Matcher::DataInputGetResult TransportProtocolInput::get(const MatchingData& data) const {
  if (!data.socket().detectedTransportProtocol().empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.socket().detectedTransportProtocol())};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

Matcher::DataInputGetResult ApplicationProtocolInput::get(const MatchingData& data) const {
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrJoin(data.socket().requestedApplicationProtocols(), ",")};
}

REGISTER_FACTORY(DestinationIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(DestinationPortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourcePortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(DirectSourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourceTypeInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(ServerNameInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(TransportProtocolInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(ApplicationProtocolInputFactory, Matcher::DataInputFactory<MatchingData>);

} // namespace Matching
} // namespace Network
} // namespace Envoy
