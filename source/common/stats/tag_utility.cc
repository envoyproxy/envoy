#include "common/stats/tag_utility.h"
#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

SymbolTable::StoragePtr addTagSuffix(StatName name, const StatNameTagVector& tags, SymbolTable& symbol_table) {
  std::vector<StatName> stat_names;
  stat_names.reserve(1 + 2 * tags.size());
  stat_names.emplace_back(name);

  for (const auto& tag : tags) {
    stat_names.emplace_back(tag.first);
    stat_names.emplace_back(tag.second);
  }

  return symbol_table.join(stat_names);
}
}
}
}