#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/interval_set.h"
#include "envoy/common/pure.h"
#include "envoy/stats/tag.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class TagExtractionContext;

/**
 * Class to extract tags from the stat names.
 */
class TagExtractor {
public:
  virtual ~TagExtractor() = default;

  /**
   * Identifier for the tag extracted by this object.
   */
  virtual absl::string_view name() const PURE;

  /**
   * Finds tags for stat_name and adds them to the tags vector. If the tag is not
   * represented in the name, the tags vector will remain unmodified. Also finds the
   * character indexes for the tags in stat_name and adds them to remove_characters (an
   * in/out arg). Returns true if a tag-match was found. The characters removed from the
   * name may be different from the values put into the tag vector for readability
   * purposes. Note: The extraction process is expected to be run iteratively, aggregating
   * the character intervals to be removed from the name after all the tag extractions are
   * complete. This approach simplifies the tag searching process because without mutations,
   * the tag extraction will be order independent, apart from the order of the tag array.
   * @param context identifies the name from which to extract tags.
   * @param tags list of tags updated with the tag name and value if found in the name.
   * @param remove_characters set of intervals of character-indices to be removed from name.
   * @return bool indicates whether a tag was found in the name.
   */
  virtual bool extractTag(TagExtractionContext& context, TagVector& tags,
                          IntervalSet<size_t>& remove_characters) const PURE;

  /**
   * Finds a prefix string associated with the matching criteria owned by the
   * extractor. This is used to reduce the number of extractors required for
   * processing each stat, by pulling the first "."-separated token on the tag.
   *
   * If a prefix cannot be extracted, an empty string_view is returned, and the
   * matcher must be applied on all inputs.
   *
   * The storage for the prefix is owned by the TagExtractor.
   *
   * @return absl::string_view the prefix, or an empty string_view if none was found.
   */
  virtual absl::string_view prefixToken() const PURE;

  virtual bool otherExtractorWithSameNameExists() const PURE;
  virtual void setOtherExtractorWithSameNameExists(bool e) PURE;
};

using TagExtractorPtr = std::unique_ptr<TagExtractor>;

} // namespace Stats
} // namespace Envoy
