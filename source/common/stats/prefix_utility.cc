#include "source/common/stats/prefix_utility.h"

#include <optional>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/config/well_known_names.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Stats {

namespace {

constexpr absl::string_view HTTP_PREFIX = "http.";
constexpr absl::string_view CLUSTER_PREFIX = "cluster.";

// Extracts the parent prefix's single tag: "http.<x>." -> {HTTP_CONN_MANAGER_PREFIX, x},
// "cluster.<x>." -> {CLUSTER_NAME, x}, anything else -> none. The trailing dot is stripped first.
// `prefix` must outlive the returned value.
std::optional<TagStringView> extractParentTag(absl::string_view prefix) {
  if (!absl::EndsWith(prefix, ".")) {
    return std::nullopt;
  }
  prefix.remove_suffix(1);

  if (absl::StartsWith(prefix, HTTP_PREFIX)) {
    return TagStringView{Envoy::Config::TagNames::get().HTTP_CONN_MANAGER_PREFIX,
                         prefix.substr(HTTP_PREFIX.size())};
  }
  if (absl::StartsWith(prefix, CLUSTER_PREFIX)) {
    return TagStringView{Envoy::Config::TagNames::get().CLUSTER_NAME,
                         prefix.substr(CLUSTER_PREFIX.size())};
  }
  return std::nullopt;
}

} // namespace

TaggedStatName mergeStatPrefix(SymbolTable& symbol_table, absl::string_view prefix,
                               absl::string_view base_name, TagStringViewSpan tags,
                               absl::string_view name) {
  // With no own tags the own prefix has no variable segment, so its tagged and tag-extracted forms
  // are identical; callers need only supply base_name.
  if (tags.empty()) {
    name = base_name;
  } else {
    ASSERT(!name.empty(), "When tags are supplied, the caller must supply the tagged name with the "
                          "tag values interleaved.");
  }

  absl::InlinedVector<TagStringView, 2> merged_tags;

  // The parent (HCM/cluster) prefix contributes at most one tag; the tag-extracted base then uses
  // just its root ("http"/"cluster"). An unrecognized parent is kept verbatim and untagged.
  absl::string_view base_prefix = prefix;
  const std::optional<TagStringView> prefix_tag = extractParentTag(prefix);
  if (prefix_tag.has_value()) {
    merged_tags.push_back(*prefix_tag);
    ASSERT(prefix.find('.') != absl::string_view::npos);
    // Keep the trailing dot of the base part of the parent prefix, so that the final base is
    // "<base_prefix><base_name>".
    base_prefix = prefix.substr(0, prefix.find('.') + 1);
  }
  merged_tags.insert(merged_tags.end(), tags.begin(), tags.end());

  // This helper function assumes the caller has already handled the dot correctly and then we can
  // concatenate the two prefixes directly. The TaggedStatName then sanitizes the name, base, and
  // tags to remove any leading or trailing dot.
  const std::string tagged = absl::StrCat(prefix, name);

  // When no tag is contributed (neither the parent prefix nor the input tags), base_prefix is the
  // full parent prefix and name equals base_name, so the base and tagged forms are identical; reuse
  // the tagged name instead of building an identical base string.
  if (merged_tags.empty()) {
    ASSERT(tagged == absl::StrCat(base_prefix, base_name));
    return {symbol_table, tagged, merged_tags, tagged};
  }
  const std::string base = absl::StrCat(base_prefix, base_name);
  return {symbol_table, base, merged_tags, tagged};
}

} // namespace Stats
} // namespace Envoy
