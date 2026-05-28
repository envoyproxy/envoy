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
                    absl::optional<StatNameTagSpan> stat_name_tags, SymbolTable& symbol_table);

  /**
   * Combines a scope's tag-extracted prefix, prefix tags, and tagged (flat) prefix, together with
   * a stat's tag-extracted name, name tags and optional tagged (flat) name, into a single joined
   * name. This is used by the tag-aware scope implementations to avoid needing to re-parse the
   * final flat stat name in order to extract tags from it.
   *
   * @param prefix StatName the tag-extracted prefix for this scope (no tag values).
   * @param prefix_tags StatNameTagSpan the tags inherited from parent scopes.
   * @param tagged_prefix StatName the flat prefix for this scope with tag values interleaved. This
   * is not optional because the scope should have generated this when the scope was created.
   * @param name StatName the tag-extracted name for this stat (no tag values).
   * @param name_tags StatNameTagSpan the tags provided by the caller for this stat.
   * @param tagged_name StatName optional flat stat name (with tag values) relative to the scope.
   * If empty, the tag-extracted name + name_tags will be used to derive the flat name.
   * If non-empty, the caller is responsible for ensuring the tagged_name is consistent with the
   * name and name_tags.
   */
  TagStatNameJoiner(StatName prefix, StatNameTagSpan prefix_tags, StatName tagged_prefix,
                    StatName name, StatNameTagSpan name_tags, StatName tagged_name,
                    SymbolTable& symbol_table);

  /**
   * @return StatName the full stat name, including the tag suffix.
   */
  StatName nameWithTags() const { return name_with_tags_; }

  /**
   * @return StatName the stat name without the tags appended.
   */
  StatName tagExtractedName() const { return tag_extracted_name_; }

  /**
   * @return the optional effective tags. absl::nullopt (no explicit tags) is preserved so that
   *         downstream code performs tag extraction on the name.
   *
   * Tags come from one of two mutually-exclusive sources depending on the constructor used:
   * the legacy constructor stores the caller-owned span directly (`stat_name_tags_`); the
   * tag-aware constructor derives and owns them in `effective_tags_`.
   */
  absl::optional<StatNameTagSpan> effectiveTags() const {
    if (stat_name_tags_.has_value()) {
      return stat_name_tags_;
    }
    if (!effective_tags_.empty()) {
      return StatNameTagSpan(effective_tags_);
    }
    return absl::nullopt;
  }

private:
  // TODO(snowp): This isn't really "tag extracted", but we'll use this for the sake of consistency
  // until we can change the naming convention throughout.
  StatName tag_extracted_name_;
  SymbolTable::StoragePtr prefix_storage_;
  SymbolTable::StoragePtr full_name_storage_;
  StatName name_with_tags_;

  // Set only by the legacy constructor: a (non-owning) span of the caller-provided tags. The
  // TagStatNameJoiner must not outlive those tags, so callers must keep them valid for its
  // lifetime. effectiveTags() returns this directly when set.
  absl::optional<StatNameTagSpan> stat_name_tags_;

  // Set only by the tag-aware constructor: the inherited (prefix) tags followed by this
  // element's own (name) tags, owned (copied) by this joiner. effectiveTags() returns a span over
  // this when it is non-empty.
  StatNameTagVec effective_tags_;

  SymbolTable::StoragePtr joinNameAndTags(StatName tagged_prefix, StatName name,
                                          StatNameTagSpan name_tags, SymbolTable& symbol_table);
};

bool isTagNameValid(absl::string_view name);

bool isTagValueValid(absl::string_view value);

} // namespace TagUtility
} // namespace Stats
} // namespace Envoy
