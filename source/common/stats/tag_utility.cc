#include "common/stats/tag_utility.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table) {
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  if (stat_name_tags) {
    full_name_storage_ =
        joinNameAndTags(StatName(prefix_storage_.get()), *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = StatName(prefix_storage_.get());
  }
}

TagStatNameJoiner::TagStatNameJoiner(StatName stat_name,
                                     StatNameTagVectorOptConstRef stat_name_tags,
                                     SymbolTable& symbol_table) {
  tag_extracted_name_ = stat_name;

  if (stat_name_tags) {
    full_name_storage_ = joinNameAndTags(stat_name, *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = stat_name;
  }
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