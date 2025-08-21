#include "source/extensions/matching/network/application_protocol/config.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Network {
namespace Matching {

Matcher::DataInputGetResult ApplicationProtocolInput::get(const MatchingData& data) const {
  const auto& protocols = data.socket().requestedApplicationProtocols();
  if (!protocols.empty()) {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            absl::StrCat("'", absl::StrJoin(protocols, "','"), "'")};
  }
  return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
}

REGISTER_FACTORY(ApplicationProtocolInputFactory, Matcher::DataInputFactory<MatchingData>);

} // namespace Matching
} // namespace Network
} // namespace Envoy
