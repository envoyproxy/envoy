#pragma once

#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "source/common/stats/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Merges a parent prefix with an owner's own name and tags to produce a TaggedStatName. This helper
 * extracts the well-known tag from the parent prefix and merges it with the input tags to produce
 * the final TaggedStatName.
 *
 * The helper assumes the caller has already handled the dots correctly, so it can concatenate the
 * parent prefix and the own name directly.
 *
 * The merged tagged name is `<prefix><name>` and the merged base name is
 * `<parent_root><base_name>`, where `<parent_root>` is `<prefix>` with its well-known tag value
 * removed (e.g. "http.ingress." -> "http."), or the full `<prefix>` when it carries no well-known
 * tag. When neither the parent prefix nor `tags` contributes a tag, the base and tagged names are
 * identical.
 *
 * @param symbol_table the symbol table used to pre-encode the names and tags.
 * @param prefix the parent prefix, whose well-known variable segment is extracted as a tag when
 *        the prefix ends with a trailing '.' (e.g. "http.<hcm>." / "cluster.<name>."); may be
 *        empty.
 * @param base_name the tag-extracted own name (every own tag value removed).
 * @param tags one {tag_name, value} per variable segment of the own name. When empty,
 *        `name` is ignored and `base_name` is used for both forms.
 * @param name the tagged own name (own tag values interleaved). Combined with `prefix` this must
 *        equal the legacy flat prefix. Ignored when `tags` is empty.
 */
TaggedStatName mergeStatPrefix(SymbolTable& symbol_table, absl::string_view prefix,
                               absl::string_view base_name, TagStringViewSpan tags = {},
                               absl::string_view name = {});

} // namespace Stats
} // namespace Envoy
