#include "common/stats/tag_utility.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     const StatNameTagVector& stat_name_tags,
                                     SymbolTable& symbol_table) {
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  name_ = StatName(prefix_storage_.get());
  full_name_storage_ =
      joinNameAndTags(StatName(prefix_storage_.get()), stat_name_tags, symbol_table);
}

TagStatNameJoiner::TagStatNameJoiner(StatName stat_name, const StatNameTagVector& stat_name_tags,
                                     SymbolTable& symbol_table) {
  name_ = stat_name;
  full_name_storage_ = joinNameAndTags(stat_name, stat_name_tags, symbol_table);
}

SymbolTable::StoragePtr TagStatNameJoiner::joinNameAndTags(StatName name,
                                                           const StatNameTagVector& tags,
                                                           SymbolTable& symbol_table) {
  std::vector<StatName> stat_names;
  stat_names.reserve(1 + 2 * tags.size());
  stat_names.emplace_back(name);

  for (const auto& tag : tags) {
    stat_names.emplace_back(tag.first);
    stat_names.emplace_back(tag.second);
  }

  return symbol_table.join(stat_names);
}
} // namespace TagUtility
} // namespace Stats
} // namespace Envoy