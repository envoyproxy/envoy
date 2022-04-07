#pragma once

#include "source/common/http/matching/inputs.h"
#include "source/common/network/matching/inputs.h"
#include "source/extensions/filters/common/rbac/matching/data.h"

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

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
