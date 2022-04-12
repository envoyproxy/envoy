#include "source/extensions/filters/common/rbac/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "data.h"

namespace Envoy {
namespace Network {
namespace Matching {

template <>
Matcher::DataInputGetResult
Network::Matching::DestinationIPInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::
    get(const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto& address = data.streamInfo().downstreamAddressProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

template <>
Matcher::DataInputGetResult
Network::Matching::DestinationPortInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::
    get(const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto& address = data.streamInfo().downstreamAddressProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
}

template <>
Matcher::DataInputGetResult
Network::Matching::SourceIPInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::get(
    const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto& address = data.streamInfo().downstreamAddressProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

template <>
Matcher::DataInputGetResult
Network::Matching::SourcePortInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::get(
    const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto& address = data.streamInfo().downstreamAddressProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          absl::StrCat(address->ip()->port())};
}

template <>
Matcher::DataInputGetResult
DirectSourceIPInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::get(
    const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto& address = data.streamInfo().downstreamAddressProvider().directRemoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
          address->ip()->addressAsString()};
}

template <>
Matcher::DataInputGetResult
ServerNameInput<Extensions::Filters::Common::RBAC::Matching::MatchingData>::get(
    const Extensions::Filters::Common::RBAC::Matching::MatchingData& data) const {
  const auto server_name = data.connection().requestedServerName();
  if (!server_name.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(server_name)};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
}

} // namespace Matching
} // namespace Network
} // namespace Envoy

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

REGISTER_FACTORY(DestinationIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(DestinationPortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(SourcePortInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(DirectSourceIPInputFactory, Matcher::DataInputFactory<MatchingData>);
REGISTER_FACTORY(ServerNameInputFactory, Matcher::DataInputFactory<MatchingData>);

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
