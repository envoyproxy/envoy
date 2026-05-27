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
   * @param prefix StatName the stat prefix to use.
   * @param name StatName the stat name to use.
   * @param stat_name_tags optional explicit tags to add to the stat name. absl::nullopt means no
   *        explicit tags were provided, in which case tag extraction will later be performed on
   *        the name. A present-but-empty span means "explicitly no tags" and suppresses extraction.
   *        This nullopt-vs-present distinction is preserved and surfaced via effectiveTags().
   */
  TagStatNameJoiner(StatName prefix, StatName stat_name,
                    absl::optional<StatNameTagSpan> stat_name_tags, SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including the tag suffix.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

  /**
   * @return the optional effective tags, exactly as provided to the constructor. absl::nullopt
   *         (no explicit tags) is preserved so that downstream code performs tag extraction on
   *         the name.
   */
  absl::optional<StatNameTagSpan> effectiveTags() const { return stat_name_tags_; }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;

  // The tags are provided to the constructor. The TagStatNameJoiner must not outlive the
  // tags; therefore, it is safe to keep a view here.
  absl::optional<StatNameTagSpan> stat_name_tags_;

  SymbolTable::StoragePtr joinNameAndTags(StatName name, StatNameTagSpan stat_name_tags,
                                          SymbolTable& symbol_table);
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

/**
 * Converts the legacy optional-reference tag representation to an
 * absl::optional<StatNameTagSpan>, preserving the nullopt-vs-present distinction (absl::nullopt
 * stays absl::nullopt). This is a convenience for call-sites that still hold a
 * StatNameTagVectorOptConstRef but need to call the absl::optional<StatNameTagSpan>-based Scope
 * APIs.
 */
inline absl::optional<StatNameTagSpan> toStatNameTagSpan(StatNameTagVectorOptConstRef tags) {
  if (tags.has_value()) {
    return StatNameTagSpan(tags->get());
  }
  return absl::nullopt;
}

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
