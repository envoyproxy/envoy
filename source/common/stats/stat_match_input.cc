#include "source/common/stats/stat_match_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Stats {
namespace Matching {
REGISTER_FACTORY(StatFullNameMatchInputFactory,
                 Matcher::DataInputFactory<Envoy::Stats::StatMatchingData>);

} // namespace Matching
} // namespace Stats
} // namespace Envoy
