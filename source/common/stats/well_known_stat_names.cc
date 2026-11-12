#include "source/common/stats/well_known_stat_names.h"

namespace Envoy {
namespace Stats {

#define INIT_WELL_KNOWN_TOKEN(name) , name##_(pool_.add(#name))
#define INIT_WELL_KNOWN_TAG(name) , tag_##name##_(pool_.add("envoy." #name))

WellKnownStatNames::WellKnownStatNames(SymbolTable& symbol_table)
    : pool_(symbol_table) WELL_KNOWN_STAT_TOKENS(INIT_WELL_KNOWN_TOKEN)
          WELL_KNOWN_STAT_TAGS(INIT_WELL_KNOWN_TAG) {}

} // namespace Stats
} // namespace Envoy
