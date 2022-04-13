#include "source/extensions/filters/common/rbac/matching/inputs.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

using DestinationIPInputFactory = Network::Matching::DestinationIPInputBaseFactory<MatchingData>;
using DestinationPortInputFactory =
    Network::Matching::DestinationPortInputBaseFactory<MatchingData>;
using SourceIPInputFactory = Network::Matching::SourceIPInputBaseFactory<MatchingData>;
using SourcePortInputFactory = Network::Matching::SourcePortInputBaseFactory<MatchingData>;
using DirectSourceIPInputFactory = Network::Matching::DirectSourceIPInputBaseFactory<MatchingData>;
using ServerNameInputFactory = Network::Matching::ServerNameBaseFactory<MatchingData>;

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
