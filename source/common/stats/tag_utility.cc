#include "source/common/stats/tag_utility.h"

#include <regex>

#include "source/common/config/well_known_names.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatName stat_name,
                                     std::optional<StatNameTagSpan> stat_name_tags,
                                     SymbolTable& symbol_table)
    : stat_name_tags_(stat_name_tags) {
  prefix_storage_ = symbol_table.join({prefix, stat_name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  if (stat_name_tags.has_value() && !stat_name_tags->empty()) {
    full_name_storage_ =
        joinNameAndTags({}, StatName(prefix_storage_.get()), *stat_name_tags, symbol_table);
    name_with_tags_ = StatName(full_name_storage_.get());
  } else {
    name_with_tags_ = StatName(prefix_storage_.get());
  }
}

TagStatNameJoiner::TagStatNameJoiner(StatName prefix, StatNameTagSpan prefix_tags,
                                     StatName tagged_prefix, StatName name,
                                     StatNameTagSpan name_tags, StatName tagged_name,
                                     SymbolTable& symbol_table) {
  // The final tag-extracted name never includes tag values: it is the scope's tag-extracted
  // prefix joined with the stat's tag-extracted name.
  prefix_storage_ = symbol_table.join({prefix, name});
  tag_extracted_name_ = StatName(prefix_storage_.get());

  // Effective tags = prefix tags (inherited from the scope) + name tags (provided by the
  // caller). These are copied into effective_tags_, owned by this Joiner; the underlying symbols
  // must remain valid (in the symbol table) for the Joiner's lifetime.
  effective_tags_.reserve(prefix_tags.size() + name_tags.size());
  effective_tags_.insert(effective_tags_.end(), prefix_tags.begin(), prefix_tags.end());
  effective_tags_.insert(effective_tags_.end(), name_tags.begin(), name_tags.end());

  if (effective_tags_.empty()) {
    // No tags: the tagged prefix and tagged name are strictly the same as the tag-extracted
    // prefix and tag-extracted name, respectively. In this case we can skip joining and just
    // reuse the tag-extracted name as the tagged (full) name.
    name_with_tags_ = tag_extracted_name_;
    return;
  }

  if (name_tags.empty()) {
    // We have prefix tags but no name tags. The stat full tagged name can be derived by joining the
    // scope's tagged prefix with the tag-extracted name -- no additional tag values to interleave.
    full_name_storage_ = symbol_table.join({tagged_prefix, name});
    name_with_tags_ = StatName(full_name_storage_.get());
    return;
  }

  if (!tagged_name.empty()) {
    // The caller has provided an explicit tagged name. Join the scope's tagged prefix with the
    // provided tagged name to get the final flat name.
    full_name_storage_ = symbol_table.join({tagged_prefix, tagged_name});
    name_with_tags_ = StatName(full_name_storage_.get());
    return;
  }

  // The last case is that we have name tags but no explicit tagged name. Join the tagged prefix,
  // tag-extracted name and name tags together to derive the stat full tagged name.
  full_name_storage_ = joinNameAndTags(tagged_prefix, name, name_tags, symbol_table);
  name_with_tags_ = StatName(full_name_storage_.get());
}

SymbolTable::StoragePtr TagStatNameJoiner::joinNameAndTags(StatName tagged_prefix, StatName name,
                                                           StatNameTagSpan name_tags,
                                                           SymbolTable& symbol_table) {
  StatNameVec stat_names;
  stat_names.reserve(2 + 2 * name_tags.size());
  stat_names.emplace_back(tagged_prefix);
  stat_names.emplace_back(name);

  for (const auto& tag : name_tags) {
    stat_names.emplace_back(tag.first);
    stat_names.emplace_back(tag.second);
  }

  return symbol_table.join(stat_names);
}

bool isTagValueValid(absl::string_view name) {
  return Config::doesTagNameValueMatchInvalidCharRegex(name);
}

bool isTagNameValid(absl::string_view value) {
  for (const auto& token : value) {
    if (!absl::ascii_isalnum(token)) {
      return false;
    }
  }
  return true;
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
