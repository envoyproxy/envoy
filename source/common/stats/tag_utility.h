#pragma once

#include "envoy/stats/tag.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {
namespace TagUtility {

/**
 * Combines a stat name with an optional set of tag to create the final stat name to use. The
 * resulting StatNames will be valid through the lifetime of this object and all provided stat
 * names.
 */
class TagStatNameJoiner {
public:
  /**
   * Combines a prefix, stat name and tags into a single stat name.
   * @param prefix StaName the stat prefix to use.
   * @param name StaName the stat name to use.
   * @param stat_name_tags optionally StatNameTagVector the stat name tags to add to the stat name.
   */
  TagStatNameJoiner(StatName prefix, StatName stat_name,
                    StatNameTagVectorOptConstRef stat_name_tags, SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including the tag suffix.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;

  SymbolTable::StoragePtr joinNameAndTags(StatName name, const StatNameTagVector& stat_name_tags,
                                          SymbolTable& symbol_table);
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
